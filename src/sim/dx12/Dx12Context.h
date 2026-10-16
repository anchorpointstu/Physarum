#pragma once

#include "sim/dx12/Dx12Buffer.h"
#include "sim/dx12/ShaderIds.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace phys::dx {

constexpr uint32_t kMaxUavs = 8;

// Buffers bound to u0..u7 for one dispatch. Unused slots stay null.
struct Bindings {
    Dx12Buffer* uav[kMaxUavs] = {};
};

// One D3D12 device with a compute queue, a single command list and a constant ring.
// Root signature (shared by every shader):
//   [0] root CBV b0   per-dispatch constants
//   [1] root CBV b1   frame constants (SimParams), "sticky" across dispatches
//   [2..9] root UAV u0..u7   raw / structured buffers
// The submission model is synchronous: Flush() executes and waits. Not thread-safe;
// callers serialise access.
class Dx12Context {
public:
    struct Options {
        bool warp = false;       // use the software WARP adapter (CLI testing)
        bool debugLayer = false; // enable the D3D12 debug layer if installed
        // Receives debug-layer messages (drained after every Flush) when the layer is active.
        std::function<void(const char*)> debugMessage;
    };

    static std::unique_ptr<Dx12Context> Create(const Options& options); // throws Dx12Error
    ~Dx12Context();

    Dx12Context(const Dx12Context&) = delete;
    Dx12Context& operator=(const Dx12Context&) = delete;

    ID3D12Device* Device() const { return device_.Get(); }
    const std::string& AdapterName() const { return adapterName_; }
    uint64_t DedicatedVideoMemory() const { return dedicatedVram_; }
    bool DebugLayerActive() const { return infoQueue_ != nullptr; }

    Dx12Buffer CreateBuffer(uint64_t bytes, HeapKind kind, const wchar_t* debugName);

    // Copies data into the constant ring and binds it to b1 for every following dispatch.
    void SetFrameConstants(const void* data, uint32_t bytes);

    void Dispatch(ShaderId shader, const void* constants, uint32_t constantBytes, const Bindings& bindings,
                  uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1);

    void CopyBuffer(Dx12Buffer& dst, uint64_t dstOffset, Dx12Buffer& src, uint64_t srcOffset, uint64_t bytes);

    // Copies `bytes` from src into a readback buffer, flushes, and hands the mapped memory to consume.
    void Readback(Dx12Buffer& src, uint64_t bytes, const std::function<void(const uint8_t*)>& consume);

    // Executes all recorded work and waits for the GPU. Cheap when nothing is recorded.
    void Flush();

    static uint32_t Groups(uint64_t items, uint32_t groupSize);

private:
    Dx12Context() = default;
    void Init(const Options& options);
    void CreateRootSignatureAndPipelines();
    void BeginList();
    void Require(Dx12Buffer& buffer, D3D12_RESOURCE_STATES state);
    D3D12_GPU_VIRTUAL_ADDRESS PushRing(const void* data, uint32_t bytes);
    void EnsureRingSpace(uint32_t bytes);
    void DrainDebugMessages();

    ComPtr<IDXGIFactory4> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    uint64_t fenceValue_ = 0;

    ComPtr<ID3D12InfoQueue> infoQueue_;
    std::function<void(const char*)> debugMessage_;
    ComPtr<ID3D12RootSignature> rootSignature_;
    std::vector<ComPtr<ID3D12PipelineState>> pipelines_;

    Dx12Buffer ring_;
    uint8_t* ringCpu_ = nullptr;
    uint64_t ringOffset_ = 0;

    std::vector<uint8_t> frameConstants_;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstantsGpu_ = 0;
    bool frameConstantsDirty_ = false;

    Dx12Buffer readback_;
    Dx12Buffer dummy_;

    uint64_t epoch_ = 1;   // incremented on every Flush; buffer states older than this are COMMON
    bool recording_ = false;
    uint32_t recordedCommands_ = 0;

    std::string adapterName_;
    uint64_t dedicatedVram_ = 0;
};

} // namespace phys::dx
