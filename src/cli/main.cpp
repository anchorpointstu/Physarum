// physarum_cli: drives the simulation core without After Effects. Used to check the
// look of the simulation, its speed, and that it is deterministic.

#include "cli/Args.h"
#include "cli/ImageWrite.h"
#include "sim/SimInstance.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

using namespace phys;

namespace {

using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

uint64_t Fnv1a(const std::vector<uint8_t>& v)
{
    uint64_t h = 1469598103934665603ull;
    for (uint8_t c : v) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

struct Runner {
    dx::Dx12Context& ctx;
    const cli::Args& args;
    const SimParams& params;
    OutputDesc out;

    std::vector<uint8_t> Present(SimInstance& sim)
    {
        std::vector<uint8_t> pixels(size_t(out.rowBytes) * out.height);
        sim.Present(params, out, pixels.data());
        return pixels;
    }

    void Configure(SimInstance& sim)
    {
        sim.Configure(Geometry{params.simW, params.simH, params.agentCount});
    }
};

// Converts one output frame (in any of AE's formats) to 8-bit RGBA, flattened over black.
std::vector<uint8_t> ToRgba8(const std::vector<uint8_t>& src, const OutputDesc& out)
{
    std::vector<uint8_t> rgba(size_t(out.width) * out.height * 4);
    for (uint32_t y = 0; y < out.height; ++y) {
        const uint8_t* row = src.data() + size_t(y) * out.rowBytes;
        for (uint32_t x = 0; x < out.width; ++x) {
            float argb[4];
            if (out.format == PixelFormat::U8) {
                for (int c = 0; c < 4; ++c) argb[c] = row[x * 4 + c] / 255.0f;
            } else if (out.format == PixelFormat::U16) {
                uint16_t v[4];
                std::memcpy(v, row + x * 8, 8);
                for (int c = 0; c < 4; ++c) argb[c] = v[c] / 32768.0f;
            } else {
                std::memcpy(argb, row + x * 16, 16);
            }
            uint8_t* d = rgba.data() + (size_t(y) * out.width + x) * 4;
            for (int c = 0; c < 3; ++c)
                d[c] = static_cast<uint8_t>(std::clamp(argb[c + 1] * argb[0], 0.0f, 1.0f) * 255.0f + 0.5f);
            d[3] = 255;
        }
    }
    return rgba;
}

int RunStraight(Runner& r, bool writeImages)
{
    SimInstance sim(r.ctx);
    r.Configure(sim);
    const auto& a = r.args;
    double stepMs = 0.0, presentMs = 0.0;
    uint32_t stepped = 0;

    for (uint32_t f = 0; f < a.frames; ++f) {
        const auto t0 = Clock::now();
        if (f == 0) sim.Reset(r.params);
        else sim.StepFrame(r.params);
        r.ctx.Flush();
        const auto t1 = Clock::now();
        const bool write = writeImages && ((a.every && f % a.every == 0) || f + 1 == a.frames);
        if (write || f + 1 == a.frames) {
            std::vector<uint8_t> px = r.Present(sim);
            const auto t2 = Clock::now();
            presentMs += Ms(t1, t2);
            if (write) {
                char name[64];
                std::snprintf(name, sizeof(name), "frame_%04u.png", f);
                const std::string path = (std::filesystem::path(a.outDir) / name).string();
                if (!cli::WritePngRgba8(path, r.out.width, r.out.height, ToRgba8(px, r.out))) {
                    std::fprintf(stderr, "error: cannot write %s\n", path.c_str());
                    return 1;
                }
                std::printf("wrote %s\n", path.c_str());
            }
        }
        if (f > 0) {
            stepMs += Ms(t0, t1);
            ++stepped;
        }
    }
    if (stepped) std::printf("step: %.3f ms/frame (%u frames, %u steps each)\n", stepMs / stepped, stepped, r.params.stepsPerFrame);
    std::printf("present + readback (last frame): %.3f ms\n", presentMs);
    std::printf("GPU memory: %.1f MB\n", sim.VramBytes() / 1048576.0);
    return 0;
}

int RunSelfcheck(Runner& r)
{
    std::vector<uint64_t> first;
    for (int run = 0; run < 2; ++run) {
        SimInstance sim(r.ctx);
        r.Configure(sim);
        for (uint32_t f = 0; f < r.args.frames; ++f) {
            if (f == 0) sim.Reset(r.params);
            else sim.StepFrame(r.params);
            const uint64_t h = Fnv1a(r.Present(sim));
            if (run == 0) {
                first.push_back(h);
            } else if (h != first[f]) {
                std::printf("NOT DETERMINISTIC: frame %u differs between runs\n", f);
                return 2;
            }
        }
    }
    std::printf("DETERMINISTIC: %u frames identical across two runs\n", r.args.frames);
    return 0;
}

int RunSnapshotCheck(Runner& r)
{
    const uint32_t k = r.args.snapshotCheck;
    const uint64_t budget = uint64_t(r.args.cacheMb) << 20;
    const int last = static_cast<int>(r.args.frames) - 1;

    // Straight run, taking snapshots every k frames, remembering every frame's image.
    SimInstance sim(r.ctx);
    r.Configure(sim);
    std::vector<uint64_t> straight;
    for (int f = 0; f <= last; ++f) {
        if (f == 0) sim.Reset(r.params);
        else sim.StepFrame(r.params);
        if (f % static_cast<int>(k) == 0) sim.TakeSnapshot(phys::StateTag{}, budget);
        straight.push_back(Fnv1a(r.Present(sim)));
    }
    const std::vector<int> kept = sim.SnapshotFrames();
    std::printf("snapshots kept (%zu, %.1f MB each, budget %u MB):", kept.size(), sim.SnapshotCost() / 1048576.0,
                r.args.cacheMb);
    for (int f : kept) std::printf(" %d", f);
    std::printf("\n");
    if (kept.empty()) {
        std::printf("SNAPSHOT CHECK FAILED: no snapshot fits in the budget\n");
        return 3;
    }

    // Random access: restore the newest snapshot at or before each target, step, compare.
    const int targets[] = {last, last / 2 + 1, static_cast<int>(k) + 1, 1, 0, last - 1};
    for (int t : targets) {
        if (t < 0 || t > last) continue;
        int from = -1;
        for (int f : kept)
            if (f <= t) from = f;
        if (from < 0) {
            sim.Reset(r.params);
            from = 0;
        } else if (!sim.RestoreSnapshot(from)) {
            std::printf("SNAPSHOT CHECK FAILED: restore of frame %d failed\n", from);
            return 3;
        }
        while (sim.LiveFrame() < t) sim.StepFrame(r.params);
        if (Fnv1a(r.Present(sim)) != straight[t]) {
            std::printf("SNAPSHOT CHECK FAILED: frame %d restored from %d differs from the straight run\n", t, from);
            return 3;
        }
        std::printf("frame %d restored from snapshot %d: identical\n", t, from);
    }
    std::printf("SNAPSHOTS OK\n");
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    cli::Args args;
    std::string error;
    if (!cli::ParseArgs(argc, argv, args, error)) {
        std::fprintf(stderr, "error: %s\n\n", error.c_str());
        cli::PrintUsage();
        return 64;
    }
    if (args.help) {
        cli::PrintUsage();
        return 0;
    }

    SimParams params = DefaultSimParams(args.width, args.height);
    params.background = static_cast<uint32_t>(Background::Black); // easier to inspect; override with --set
    for (const auto& [k, v] : args.settings) {
        if (!cli::ApplySetting(params, k, v, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 64;
        }
    }
    Finalize(params);

    try {
        dx::Dx12Context::Options opt;
        opt.warp = args.warp;
        opt.debugLayer = args.debugLayer;
        int debugMessages = 0;
        opt.debugMessage = [&](const char* m) {
            ++debugMessages;
            std::fprintf(stderr, "D3D12: %s\n", m);
        };
        auto ctx = dx::Dx12Context::Create(opt);
        if (args.debugLayer)
            std::printf("D3D12 debug layer: %s\n",
                        ctx->DebugLayerActive() ? "active" : "NOT AVAILABLE (install the Graphics Tools optional feature)");
        std::printf("GPU: %s (%.0f MB dedicated)\n", ctx->AdapterName().c_str(), ctx->DedicatedVideoMemory() / 1048576.0);
        std::printf("simulation %u x %u, %u frames\n", params.simW, params.simH, args.frames);
        cli::PrintParams(params);

        Runner r{*ctx, args, params, {}};
        r.out.width = args.outWidth ? args.outWidth : args.width;
        r.out.height = args.outHeight ? args.outHeight : args.height;
        r.out.format = args.bits == 8 ? PixelFormat::U8 : args.bits == 16 ? PixelFormat::U16 : PixelFormat::F32;
        r.out.rowBytes = r.out.width * BytesPerPixel(r.out.format) + 64; // padded like AE worlds can be
        r.out.scaleX = float(params.simW) / float(r.out.width);
        r.out.scaleY = float(params.simH) / float(r.out.height);

        if (args.images) std::filesystem::create_directories(args.outDir);

        int rc = 0;
        if (args.selfcheck) rc = RunSelfcheck(r);
        if (rc == 0 && args.snapshotCheck) rc = RunSnapshotCheck(r);
        if (rc == 0 && !args.selfcheck && !args.snapshotCheck) rc = RunStraight(r, args.images);
        if (ctx->DebugLayerActive()) {
            std::printf("D3D12 debug layer messages: %d\n", debugMessages);
            if (debugMessages && rc == 0) rc = 4;
        }
        return rc;
    } catch (const dx::Dx12Error& e) {
        std::fprintf(stderr, "Direct3D error: %s%s\n", e.what(), e.deviceLost() ? " [device lost]" : "");
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
