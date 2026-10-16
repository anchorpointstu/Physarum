#pragma once

#include "sim/SimParams.h"
#include "sim/dx12/Dx12Context.h"

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace phys {

// Opaque 16-byte tag stored with live state and snapshots. The After Effects layer
// stores a PF_State in it to validate cached state; the CLI leaves it zeroed.
using StateTag = std::array<uint8_t, 16>;

// The size-defining parameters. Changing any of them reallocates and drops all state.
struct Geometry {
    uint32_t simW = 0;
    uint32_t simH = 0;
    uint32_t agentCount = 0;
    bool operator==(const Geometry&) const = default;
};

// One simulation: GPU buffers, the live state, and a ring of snapshots.
// Frame 0 is the state right after spawning and the pre-roll. Frame f is frame
// f-1 advanced by stepsPerFrame steps using the parameters of frame f.
class SimInstance {
public:
    explicit SimInstance(dx::Dx12Context& ctx);

    // (Re)allocates buffers if the geometry changed. Returns true if it did,
    // which means all state was dropped.
    bool Configure(const Geometry& g);
    const Geometry& GetGeometry() const { return geometry_; }

    void Reset(const SimParams& p0);       // spawn + pre-roll; the live state becomes frame 0
    void StepFrame(const SimParams& p);    // live frame f -> f + 1
    void Present(const SimParams& p, const OutputDesc& out, void* dst);

    int LiveFrame() const { return liveFrame_; } // -1 when there is no live state
    const StateTag& LiveTag() const { return liveTag_; }
    void SetLiveTag(const StateTag& tag) { liveTag_ = tag; }
    void DropLive() { liveFrame_ = -1; }

    // Snapshots of the live state. Budget is in bytes for all snapshots together.
    bool TakeSnapshot(const StateTag& tag, uint64_t budgetBytes);
    bool RestoreSnapshot(int frame);
    std::vector<int> SnapshotFrames() const;
    const StateTag* SnapshotTag(int frame) const;
    void DropSnapshotsFrom(int frame); // drops every snapshot at or after frame
    void DropAll();

    uint64_t SnapshotCost() const;
    uint64_t SnapshotBytes() const;
    uint64_t VramBytes() const;      // live buffers + snapshots

    // Estimates the live (non-snapshot) GPU memory for a geometry.
    static uint64_t EstimateLiveBytes(const Geometry& g);

private:
    struct Snapshot {
        dx::Dx12Buffer buffer;  // agents followed by trailA
        uint32_t stepCounter = 0;
        StateTag tag{};
    };

    void Step(const SimParams& p);
    void Clear(dx::Dx12Buffer& buffer, uint64_t words);
    void Dispatch1D(dx::ShaderId shader, uint64_t items, uint32_t stepIndex, const dx::Bindings& b, uint32_t value = 0);
    void Dispatch2D(dx::ShaderId shader, uint32_t w, uint32_t h, const dx::Bindings& b);
    void SetParams(const SimParams& p);

    dx::Dx12Context& ctx_;
    Geometry geometry_{};
    uint64_t agentBytes_ = 0;
    uint64_t trailBytes_ = 0;

    dx::Dx12Buffer agents_;
    dx::Dx12Buffer trailA_;
    dx::Dx12Buffer trailB_;
    dx::Dx12Buffer trailAccum_;
    dx::Dx12Buffer overlay_;
    dx::Dx12Buffer output_;

    int liveFrame_ = -1;
    uint32_t stepCounter_ = 0;
    StateTag liveTag_{};

    std::map<int, Snapshot> snapshots_;
    std::vector<dx::Dx12Buffer> freeSnapshotBuffers_;
};

} // namespace phys
