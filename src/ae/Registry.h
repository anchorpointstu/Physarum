#pragma once

// Process-wide registry of simulation instances, keyed by the 64-bit id stored in
// each effect's sequence data, plus the shared Direct3D 12 device.
//
// After Effects renders on a copy of the project. Its sequence data is copied there
// and announced with SEQUENCE_RESETUP + PF_InFlag_PROJECT_IS_RENDER_ONLY. Those
// copies must share the UI instance's simulation, while a duplicated or pasted
// layer (a UI-side RESETUP whose id is already owned by another live UI handle)
// must get a fresh id and its own simulation.
//
// Locking: `mutex_` guards the maps and is held only briefly. GPU work and the
// destruction of simulations happen under GpuMutex(). Neither is ever held while
// calling back into the host.

#include "ae/Physarum.h"
#include "sim/SimInstance.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace phys::ae {

struct Instance {
    std::unique_ptr<SimInstance> sim; // guarded by GpuMutex()
    bool errorShown = false;          // guarded by GpuMutex()
};

class Registry {
public:
    static Registry& Get();

    uint64_t NewId();

    // Sequence lifecycle. Each returns the id the handle must carry.
    void OnSetup(PF_Handle handle, uint64_t id);
    uint64_t OnResetup(PF_Handle handle, uint64_t id, bool renderOnly);
    void OnSetdown(PF_Handle handle);

    // Returns the instance for an id, creating it if needed.
    std::shared_ptr<Instance> Acquire(uint64_t id);

    std::mutex& GpuMutex() { return gpuMutex_; }

    // Must hold GpuMutex(). Creates the device on first use; returns null and fills
    // error when no DirectX 12 device is available.
    dx::Dx12Context* EnsureDevice(std::string& error);

    // Must hold GpuMutex(). Drops every simulation and the device (after device loss).
    void DropDevice();

    // Called at GLOBAL_SETDOWN.
    void Shutdown();

private:
    struct HandleInfo {
        uint64_t id;
        bool renderOnly;
    };

    std::mutex mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<Instance>> instances_;
    std::unordered_map<uint64_t, int> uiOwners_;          // live UI handles per id
    std::unordered_map<PF_Handle, HandleInfo> handles_;

    std::mutex gpuMutex_;
    std::unique_ptr<dx::Dx12Context> device_;
};

} // namespace phys::ae
