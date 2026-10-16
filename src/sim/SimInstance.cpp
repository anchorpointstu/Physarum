#include "sim/SimInstance.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace phys {

using dx::Bindings;
using dx::Dx12Buffer;
using dx::Dx12Context;
using dx::HeapKind;
using dx::ShaderId;

namespace {
constexpr uint32_t kGroup1D = 256;
constexpr uint32_t kGroup2D = 16;
constexpr uint32_t kGroupPresent = 8;
constexpr size_t kMaxFreeSnapshotBuffers = 2;
} // namespace

SimInstance::SimInstance(Dx12Context& ctx) : ctx_(ctx) {}

uint64_t SimInstance::EstimateLiveBytes(const Geometry& g)
{
    const uint64_t texels = uint64_t(g.simW) * g.simH;
    // agents + trailA + trailB + accum (+ overlay, allocated on demand)
    return uint64_t(g.agentCount) * 16 + texels * 16 * 3;
}

bool SimInstance::Configure(const Geometry& g)
{
    if (g == geometry_ && agents_) return false;
    if (g.simW == 0 || g.simH == 0 || g.simW > kMaxSimSide || g.simH > kMaxSimSide ||
        uint64_t(g.simW) * g.simH > kMaxSimTexels || g.agentCount == 0 || g.agentCount > kMaxAgents)
        throw dx::Dx12Error(E_INVALIDARG, "Simulation size out of range", false);

    ctx_.Flush();
    DropAll();
    agents_.Reset();
    trailA_.Reset();
    trailB_.Reset();
    trailAccum_.Reset();
    overlay_.Reset();
    geometry_ = Geometry{};

    agentBytes_ = uint64_t(g.agentCount) * 16;
    trailBytes_ = uint64_t(g.simW) * g.simH * 16;
    agents_ = ctx_.CreateBuffer(agentBytes_, HeapKind::Default, L"Physarum agents");
    trailA_ = ctx_.CreateBuffer(trailBytes_, HeapKind::Default, L"Physarum trailA");
    trailB_ = ctx_.CreateBuffer(trailBytes_, HeapKind::Default, L"Physarum trailB");
    trailAccum_ = ctx_.CreateBuffer(trailBytes_, HeapKind::Default, L"Physarum trail accumulator");
    geometry_ = g;
    liveFrame_ = -1;
    return true;
}

void SimInstance::SetParams(const SimParams& p)
{
    SimParams q = p;
    q.simW = geometry_.simW;
    q.simH = geometry_.simH;
    q.agentCount = geometry_.agentCount;
    ctx_.SetFrameConstants(&q, sizeof(q));
}

void SimInstance::Dispatch1D(ShaderId shader, uint64_t items, uint32_t stepIndex, const Bindings& b, uint32_t value)
{
    if (items == 0) return;
    const uint64_t groups = (items + kGroup1D - 1) / kGroup1D;
    const uint32_t gx = static_cast<uint32_t>(std::min<uint64_t>(groups, D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION));
    const uint32_t gy = static_cast<uint32_t>((groups + gx - 1) / gx);
    DispatchConstants dc{static_cast<uint32_t>(items), gx, stepIndex, value};
    ctx_.Dispatch(shader, &dc, sizeof(dc), b, gx, gy);
}

void SimInstance::Dispatch2D(ShaderId shader, uint32_t w, uint32_t h, const Bindings& b)
{
    DispatchConstants dc{0, 0, stepCounter_, 0};
    ctx_.Dispatch(shader, &dc, sizeof(dc), b, Dx12Context::Groups(w, kGroup2D), Dx12Context::Groups(h, kGroup2D));
}

void SimInstance::Clear(Dx12Buffer& buffer, uint64_t words)
{
    Bindings b;
    b.uav[0] = &buffer;
    Dispatch1D(ShaderId::ClearUint, words, 0, b, 0);
}

void SimInstance::Reset(const SimParams& p0)
{
    if (!agents_) throw dx::Dx12Error(E_UNEXPECTED, "SimInstance not configured", false);
    SetParams(p0);
    Clear(trailA_, trailBytes_ / 4);
    Clear(trailAccum_, trailBytes_ / 4);

    Bindings b;
    b.uav[0] = &agents_;
    Dispatch1D(ShaderId::Spawn, geometry_.agentCount, 0, b);

    stepCounter_ = 1;
    for (uint32_t i = 0; i < p0.preRoll; ++i) Step(p0);
    liveFrame_ = 0;
    liveTag_ = {};
}

void SimInstance::Step(const SimParams& p)
{
    (void)p;
    {
        Bindings b;
        b.uav[0] = &agents_;
        b.uav[1] = &trailA_;
        b.uav[2] = &trailAccum_;
        Dispatch1D(ShaderId::AgentStep, geometry_.agentCount, stepCounter_, b);
    }
    {
        Bindings b;
        b.uav[0] = &trailA_;
        b.uav[1] = &trailB_;
        b.uav[2] = &trailAccum_;
        Dispatch2D(ShaderId::TrailResolve, geometry_.simW, geometry_.simH, b);
    }
    {
        Bindings b;
        b.uav[0] = &trailA_;
        b.uav[1] = &trailB_;
        Dispatch2D(ShaderId::TrailDiffuse, geometry_.simW, geometry_.simH, b);
    }
    ++stepCounter_;
}

void SimInstance::StepFrame(const SimParams& p)
{
    if (liveFrame_ < 0) throw dx::Dx12Error(E_UNEXPECTED, "StepFrame without live state", false);
    SetParams(p);
    const uint32_t steps = std::clamp<uint32_t>(p.stepsPerFrame, 1, 10);
    for (uint32_t i = 0; i < steps; ++i) Step(p);
    ++liveFrame_;
}

void SimInstance::Present(const SimParams& p, const OutputDesc& out, void* dst)
{
    if (liveFrame_ < 0) throw dx::Dx12Error(E_UNEXPECTED, "Present without live state", false);
    if (out.width == 0 || out.height == 0) return;
    SetParams(p);

    if (p.drawAgents) {
        if (!overlay_) overlay_ = ctx_.CreateBuffer(trailBytes_, HeapKind::Default, L"Physarum overlay");
        Clear(overlay_, trailBytes_ / 4);
        Bindings b;
        b.uav[0] = &agents_;
        b.uav[1] = &overlay_;
        Dispatch1D(ShaderId::AgentsDraw, geometry_.agentCount, 0, b);
    }

    const uint32_t bpp = BytesPerPixel(out.format);
    const uint64_t tightPitch = uint64_t(out.width) * bpp;
    const uint64_t bytes = tightPitch * out.height;
    if (!output_ || output_.size < bytes) {
        ctx_.Flush();
        output_.Reset();
        output_ = ctx_.CreateBuffer(bytes, HeapKind::Default, L"Physarum output");
    }

    PresentConstants pc{};
    pc.outW = out.width;
    pc.outH = out.height;
    pc.format = static_cast<uint32_t>(out.format);
    const float maxScale = std::max(out.scaleX, out.scaleY);
    pc.taps = static_cast<uint32_t>(std::clamp(std::ceil(maxScale), 1.0f, 4.0f));
    pc.originX = out.originX;
    pc.originY = out.originY;
    pc.scaleX = out.scaleX;
    pc.scaleY = out.scaleY;

    Bindings b;
    b.uav[0] = &trailA_;
    b.uav[1] = p.drawAgents ? &overlay_ : nullptr;
    b.uav[2] = &output_;
    ctx_.Dispatch(ShaderId::Present, &pc, sizeof(pc), b, Dx12Context::Groups(out.width, kGroupPresent),
                  Dx12Context::Groups(out.height, kGroupPresent));

    ctx_.Readback(output_, bytes, [&](const uint8_t* src) {
        auto* d = static_cast<uint8_t*>(dst);
        if (out.rowBytes == tightPitch) {
            std::memcpy(d, src, static_cast<size_t>(bytes));
        } else {
            for (uint32_t y = 0; y < out.height; ++y)
                std::memcpy(d + uint64_t(y) * out.rowBytes, src + y * tightPitch, static_cast<size_t>(tightPitch));
        }
    });
}

// ---------------------------------------------------------------- snapshots

uint64_t SimInstance::SnapshotCost() const { return dx::AlignUp(agentBytes_, 256) + trailBytes_; }

uint64_t SimInstance::SnapshotBytes() const { return SnapshotCost() * snapshots_.size(); }

uint64_t SimInstance::VramBytes() const
{
    uint64_t live = agents_.size + trailA_.size + trailB_.size + trailAccum_.size + overlay_.size + output_.size;
    for (const auto& b : freeSnapshotBuffers_) live += b.size;
    return live + SnapshotBytes();
}

bool SimInstance::TakeSnapshot(const StateTag& tag, uint64_t budgetBytes)
{
    if (liveFrame_ < 0) return false;
    const uint64_t cost = SnapshotCost();
    if (cost > budgetBytes) return false;

    auto existing = snapshots_.find(liveFrame_);
    if (existing != snapshots_.end()) {
        existing->second.tag = tag; // same state, refresh the tag
        return true;
    }

    // Evict until the new snapshot fits. The victim is the snapshot whose removal
    // leaves the smallest gap between its neighbours (the new frame counts as the
    // right neighbour of the newest one). Frame 0 is kept while anything else exists.
    if (SnapshotBytes() + cost > budgetBytes && !snapshots_.empty())
        ctx_.Flush(); // evicted buffers may still be referenced by recorded copies
    while (SnapshotBytes() + cost > budgetBytes && !snapshots_.empty()) {
        int victim = std::numeric_limits<int>::min();
        int bestGap = std::numeric_limits<int>::max();
        std::vector<int> frames = SnapshotFrames();
        for (size_t i = 0; i < frames.size(); ++i) {
            if (frames[i] == 0 && frames.size() > 1) continue;
            const int prev = i > 0 ? frames[i - 1] : frames[i];
            const int next = i + 1 < frames.size() ? frames[i + 1] : liveFrame_;
            const int gap = next - prev;
            if (gap < bestGap) {
                bestGap = gap;
                victim = frames[i];
            }
        }
        if (victim == std::numeric_limits<int>::min()) break;
        auto it = snapshots_.find(victim);
        if (freeSnapshotBuffers_.size() < kMaxFreeSnapshotBuffers) freeSnapshotBuffers_.push_back(std::move(it->second.buffer));
        snapshots_.erase(it);
    }
    if (SnapshotBytes() + cost > budgetBytes) return false;

    Snapshot snap;
    for (auto it = freeSnapshotBuffers_.begin(); it != freeSnapshotBuffers_.end(); ++it) {
        if (it->size >= cost) {
            snap.buffer = std::move(*it);
            freeSnapshotBuffers_.erase(it);
            break;
        }
    }
    if (!snap.buffer) snap.buffer = ctx_.CreateBuffer(cost, HeapKind::Default, L"Physarum snapshot");
    ctx_.CopyBuffer(snap.buffer, 0, agents_, 0, agentBytes_);
    ctx_.CopyBuffer(snap.buffer, dx::AlignUp(agentBytes_, 256), trailA_, 0, trailBytes_);
    snap.stepCounter = stepCounter_;
    snap.tag = tag;
    snapshots_.emplace(liveFrame_, std::move(snap));
    return true;
}

bool SimInstance::RestoreSnapshot(int frame)
{
    auto it = snapshots_.find(frame);
    if (it == snapshots_.end()) return false;
    ctx_.CopyBuffer(agents_, 0, it->second.buffer, 0, agentBytes_);
    ctx_.CopyBuffer(trailA_, 0, it->second.buffer, dx::AlignUp(agentBytes_, 256), trailBytes_);
    // The accumulator is always zero between steps, so it needs no snapshot.
    stepCounter_ = it->second.stepCounter;
    liveFrame_ = frame;
    liveTag_ = it->second.tag;
    return true;
}

std::vector<int> SimInstance::SnapshotFrames() const
{
    std::vector<int> f;
    f.reserve(snapshots_.size());
    for (const auto& kv : snapshots_) f.push_back(kv.first);
    return f;
}

const StateTag* SimInstance::SnapshotTag(int frame) const
{
    auto it = snapshots_.find(frame);
    return it == snapshots_.end() ? nullptr : &it->second.tag;
}

void SimInstance::DropSnapshotsFrom(int frame)
{
    ctx_.Flush(); // snapshot buffers may still be referenced by recorded copies
    for (auto it = snapshots_.lower_bound(frame); it != snapshots_.end();) {
        if (freeSnapshotBuffers_.size() < kMaxFreeSnapshotBuffers) freeSnapshotBuffers_.push_back(std::move(it->second.buffer));
        it = snapshots_.erase(it);
    }
}

void SimInstance::DropAll()
{
    ctx_.Flush();
    snapshots_.clear();
    freeSnapshotBuffers_.clear();
    liveFrame_ = -1;
}

// ---------------------------------------------------------------- parameters

SimParams DefaultSimParams(uint32_t simW, uint32_t simH)
{
    constexpr float kDeg = 3.14159265358979f / 180.0f;
    SimParams p{};
    p.numSpecies = 1;
    p.seed = 1;
    p.agentCount = 500'000;
    p.stepsPerFrame = 1;
    p.preRoll = 0;
    p.boundary = static_cast<uint32_t>(Boundary::Wrap);
    p.diffusionRadius = 1;
    p.palette = static_cast<uint32_t>(Palette::SpeciesColours);
    p.simW = simW;
    p.simH = simH;
    p.background = static_cast<uint32_t>(Background::Transparent);
    p.alphaFromIntensity = 1;
    p.drawAgents = 0;
    p.agentSize = 1;
    p.decay = 0.10f;
    p.diffusionStrength = 1.0f;
    p.trailClamp = 10.0f;
    p.exposure = 1.0f;
    p.gamma = 1.0f;
    p.agentOpacity = 0.5f;
    const float colA[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float colB[4] = {1.0f, 0.45f, 0.05f, 1.0f};
    const float colC[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float bg[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    std::memcpy(p.colA, colA, sizeof(colA));
    std::memcpy(p.colB, colB, sizeof(colB));
    std::memcpy(p.colC, colC, sizeof(colC));
    std::memcpy(p.bgColour, bg, sizeof(bg));

    const float colours[kMaxSpecies][3] = {{1, 1, 1}, {0, 1, 1}, {1, 0, 1}, {1, 1, 0}};
    const float shares[kMaxSpecies] = {1.0f, 1.0f, 1.0f, 1.0f}; // equal split by default
    for (uint32_t s = 0; s < kMaxSpecies; ++s) {
        SpeciesParams& sp = p.species[s];
        sp.sensorAngle = 22.5f * kDeg;
        sp.sensorDist = 9.0f;
        sp.turnAngle = 45.0f * kDeg;
        sp.stepSize = 1.0f;
        sp.deposit = 1.0f;
        sp.jitter = 0.0f;
        sp.otherWeight = -0.5f;
        sp.share = shares[s];
        sp.colour[0] = colours[s][0];
        sp.colour[1] = colours[s][1];
        sp.colour[2] = colours[s][2];
        sp.colour[3] = 1.0f;
        sp.spawnPattern = static_cast<uint32_t>(SpawnPattern::Random);
        sp.headingMode = static_cast<uint32_t>(HeadingMode::Random);
        sp.spawnCx = simW * 0.5f;
        sp.spawnCy = simH * 0.5f;
        sp.spawnSize = 0.5f;
    }
    Finalize(p);
    return p;
}

void Finalize(SimParams& p)
{
    auto clampf = [](float v, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : lo; };
    constexpr float kPi = 3.14159265358979f;

    p.numSpecies = std::clamp<uint32_t>(p.numSpecies, 1, kMaxSpecies);
    p.agentCount = std::clamp<uint32_t>(p.agentCount, 1, kMaxAgents);
    p.stepsPerFrame = std::clamp<uint32_t>(p.stepsPerFrame, 1, 10);
    p.preRoll = std::min<uint32_t>(p.preRoll, 5000);
    p.boundary = std::min<uint32_t>(p.boundary, static_cast<uint32_t>(Boundary::Count) - 1);
    p.diffusionRadius = std::min<uint32_t>(p.diffusionRadius, 2);
    p.palette = std::min<uint32_t>(p.palette, static_cast<uint32_t>(Palette::Count) - 1);
    p.background = std::min<uint32_t>(p.background, static_cast<uint32_t>(Background::Count) - 1);
    p.alphaFromIntensity = p.alphaFromIntensity ? 1 : 0;
    p.drawAgents = p.drawAgents ? 1 : 0;
    p.agentSize = std::clamp<uint32_t>(p.agentSize, 1, 4);
    p.decay = clampf(p.decay, 0.0f, 1.0f);
    p.diffusionStrength = clampf(p.diffusionStrength, 0.0f, 1.0f);
    p.trailClamp = clampf(p.trailClamp, 0.1f, 1000.0f);
    p.exposure = clampf(p.exposure, 0.0f, 100.0f);
    p.gamma = clampf(p.gamma, 0.1f, 10.0f);
    p.agentOpacity = clampf(p.agentOpacity, 0.0f, 1.0f);

    // Agent ranges per species from the shares; the remainder goes to the last species.
    float total = 0.0f;
    for (uint32_t s = 0; s < p.numSpecies; ++s) {
        p.species[s].share = clampf(p.species[s].share, 0.0f, 1.0f);
        total += p.species[s].share;
    }
    uint32_t begin = 0;
    for (uint32_t s = 0; s < kMaxSpecies; ++s) {
        SpeciesParams& sp = p.species[s];
        sp.sensorAngle = clampf(sp.sensorAngle, 0.0f, kPi);
        sp.sensorDist = clampf(sp.sensorDist, 0.0f, 1000.0f);
        sp.turnAngle = clampf(sp.turnAngle, 0.0f, kPi);
        sp.stepSize = clampf(sp.stepSize, 0.0f, 100.0f);
        sp.deposit = clampf(sp.deposit, 0.0f, 5.0f);
        sp.jitter = clampf(sp.jitter, 0.0f, kPi);
        sp.otherWeight = clampf(sp.otherWeight, -1.0f, 1.0f);
        sp.spawnPattern = std::min<uint32_t>(sp.spawnPattern, static_cast<uint32_t>(SpawnPattern::Count) - 1);
        sp.headingMode = std::min<uint32_t>(sp.headingMode, static_cast<uint32_t>(HeadingMode::Count) - 1);
        sp.spawnSize = clampf(sp.spawnSize, 0.0f, 4.0f);
        sp.spawnCx = clampf(sp.spawnCx, -1e6f, 1e6f);
        sp.spawnCy = clampf(sp.spawnCy, -1e6f, 1e6f);
        for (float& c : sp.colour) c = clampf(c, 0.0f, 1.0f);

        uint32_t count = 0;
        if (s < p.numSpecies) {
            if (s == p.numSpecies - 1) {
                count = p.agentCount - begin;
            } else {
                const double frac = total > 0.0f ? sp.share / total : 1.0 / p.numSpecies;
                count = std::min<uint32_t>(static_cast<uint32_t>(std::floor(p.agentCount * frac)), p.agentCount - begin);
            }
        }
        sp.agentBegin = begin;
        sp.agentEnd = begin + count;
        begin += count;
    }
}

} // namespace phys
