#include "ae/Registry.h"

#include <random>
#include <vector>

namespace phys::ae {

Registry& Registry::Get()
{
    static Registry registry;
    return registry;
}

uint64_t Registry::NewId()
{
    static std::mutex rngMutex;
    static std::mt19937_64 rng([] {
        std::random_device rd;
        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        return (uint64_t(rd()) << 32) ^ rd() ^ uint64_t(qpc.QuadPart) ^ uint64_t(GetCurrentProcessId());
    }());
    std::lock_guard<std::mutex> lock(rngMutex);
    uint64_t id = 0;
    while (id == 0) id = rng();
    return id;
}

void Registry::OnSetup(PF_Handle handle, uint64_t id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    handles_[handle] = HandleInfo{id, false};
    ++uiOwners_[id];
}

uint64_t Registry::OnResetup(PF_Handle handle, uint64_t id, bool renderOnly)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto known = handles_.find(handle);
    if (known != handles_.end() && known->second.id == id) return id; // same handle again

    if (renderOnly) {
        handles_[handle] = HandleInfo{id, true};
        return id;
    }

    // A UI handle whose id another live UI handle already owns is a duplicate or a
    // paste: give it its own id so the two layers simulate independently.
    auto owners = uiOwners_.find(id);
    if (id == 0 || (owners != uiOwners_.end() && owners->second > 0)) id = NewId();
    handles_[handle] = HandleInfo{id, false};
    ++uiOwners_[id];
    return id;
}

void Registry::OnSetdown(PF_Handle handle)
{
    std::shared_ptr<Instance> released;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handles_.find(handle);
        if (it == handles_.end()) return;
        const HandleInfo info = it->second;
        handles_.erase(it);
        if (info.renderOnly) return;

        auto owners = uiOwners_.find(info.id);
        if (owners != uiOwners_.end() && --owners->second <= 0) {
            uiOwners_.erase(owners);
            auto inst = instances_.find(info.id);
            if (inst != instances_.end()) {
                released = std::move(inst->second);
                instances_.erase(inst);
            }
        }
    }
    if (released) {
        // Free GPU memory now unless a render still holds the instance; then it is
        // freed when that render lets go.
        std::lock_guard<std::mutex> gpu(gpuMutex_);
        if (released.use_count() == 1) released->sim.reset();
        released.reset();
    }
}

std::shared_ptr<Instance> Registry::Acquire(uint64_t id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = instances_[id];
    if (!slot) slot = std::make_shared<Instance>();
    return slot;
}

dx::Dx12Context* Registry::EnsureDevice(std::string& error)
{
    if (device_) return device_.get();
    try {
        dx::Dx12Context::Options opt;
#if PHYSARUM_DEBUG
        char buf[8] = {};
        opt.debugLayer = GetEnvironmentVariableA("PHYSARUM_D3D_DEBUG", buf, sizeof(buf)) > 0 && buf[0] == '1';
        opt.debugMessage = [](const char* m) {
            OutputDebugStringA("Physarum D3D12: ");
            OutputDebugStringA(m);
            OutputDebugStringA("\n");
        };
#endif
        device_ = dx::Dx12Context::Create(opt);
        return device_.get();
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

void Registry::DropDevice()
{
    std::vector<std::shared_ptr<Instance>> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& kv : instances_) all.push_back(kv.second);
    }
    for (auto& inst : all) inst->sim.reset();
    device_.reset();
}

void Registry::Shutdown()
{
    std::lock_guard<std::mutex> gpu(gpuMutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& kv : instances_) kv.second->sim.reset();
        instances_.clear();
        uiOwners_.clear();
        handles_.clear();
    }
    device_.reset();
}

} // namespace phys::ae
