#include "sim/dx12/Dx12Context.h"

#include <cstring>

namespace phys::dx {

namespace {
constexpr uint64_t kRingBytes = 8ull << 20;          // constant ring (upload heap)
constexpr uint32_t kCbAlign = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT; // 256
constexpr uint32_t kMaxCommandsPerList = 2048;        // bounds GPU time per submission

D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES p{};
    p.Type = type;
    p.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    p.CreationNodeMask = 1;
    p.VisibleNodeMask = 1;
    return p;
}

D3D12_RESOURCE_DESC BufferDesc(uint64_t bytes, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Alignment = 0;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = flags;
    return d;
}

bool EnableDebugLayerIfRequested(bool requested)
{
    if (!requested) return false;
    ComPtr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) return false; // Graphics Tools not installed
    debug->EnableDebugLayer();
    return true;
}
} // namespace

std::unique_ptr<Dx12Context> Dx12Context::Create(const Options& options)
{
    std::unique_ptr<Dx12Context> ctx(new Dx12Context());
    ctx->Init(options);
    return ctx;
}

Dx12Context::~Dx12Context()
{
    try {
        if (device_ && !recording_) {
            // Make sure the GPU is idle before resources are released.
            Check(queue_->Signal(fence_.Get(), ++fenceValue_), "Signal");
            if (fence_->GetCompletedValue() < fenceValue_) {
                fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
                WaitForSingleObject(fenceEvent_, 5000);
            }
        }
    } catch (...) {
    }
    if (ring_ && ringCpu_) ring_.resource->Unmap(0, nullptr);
    if (fenceEvent_) CloseHandle(fenceEvent_);
}

void Dx12Context::Init(const Options& options)
{
    const bool debugLayer = EnableDebugLayerIfRequested(options.debugLayer);
    debugMessage_ = options.debugMessage;

    UINT factoryFlags = options.debugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0;
    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_))))
        Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)), "CreateDXGIFactory2");

    ComPtr<IDXGIAdapter1> chosen;
    if (options.warp) {
        ComPtr<IDXGIAdapter> warp;
        Check(factory_->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter");
        Check(warp.As(&chosen), "WARP adapter query");
    } else {
        // Prefer the high-performance GPU when the OS can tell us which one it is.
        ComPtr<IDXGIFactory6> factory6;
        if (SUCCEEDED(factory_.As(&factory6))) {
            for (UINT i = 0;; ++i) {
                ComPtr<IDXGIAdapter1> a;
                if (factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) ==
                    DXGI_ERROR_NOT_FOUND)
                    break;
                DXGI_ADAPTER_DESC1 d{};
                a->GetDesc1(&d);
                if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
                    chosen = a;
                    break;
                }
            }
        }
        if (!chosen) {
            for (UINT i = 0;; ++i) {
                ComPtr<IDXGIAdapter1> a;
                if (factory_->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 d{};
                a->GetDesc1(&d);
                if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
                    chosen = a;
                    break;
                }
            }
        }
        if (!chosen) throw Dx12Error(DXGI_ERROR_NOT_FOUND, "No DirectX 12 capable GPU was found", false);
    }

    DXGI_ADAPTER_DESC1 desc{};
    chosen->GetDesc1(&desc);
    adapterName_ = Narrow(desc.Description);
    dedicatedVram_ = desc.DedicatedVideoMemory;

    Check(D3D12CreateDevice(chosen.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)), "D3D12CreateDevice");
    if (debugLayer && SUCCEEDED(device_.As(&infoQueue_))) infoQueue_->ClearStoredMessages();

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    Check(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)), "CreateCommandQueue");
    Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator_)),
          "CreateCommandAllocator");
    Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator_.Get(), nullptr,
                                     IID_PPV_ARGS(&list_)),
          "CreateCommandList");
    Check(list_->Close(), "CommandList::Close");

    Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) throw Dx12Error(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent", false);

    CreateRootSignatureAndPipelines();

    dummy_ = CreateBuffer(256, HeapKind::Default, L"Physarum dummy UAV");
    ring_ = CreateBuffer(kRingBytes, HeapKind::Upload, L"Physarum constant ring");
    D3D12_RANGE noRead{0, 0};
    Check(ring_.resource->Map(0, &noRead, reinterpret_cast<void**>(&ringCpu_)), "Map constant ring", device_.Get());
}

void Dx12Context::CreateRootSignatureAndPipelines()
{
    D3D12_ROOT_PARAMETER params[2 + kMaxUavs]{};
    for (UINT i = 0; i < 2; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[i].Descriptor.ShaderRegister = i;
        params[i].Descriptor.RegisterSpace = 0;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (UINT i = 0; i < kMaxUavs; ++i) {
        auto& p = params[2 + i];
        p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        p.Descriptor.ShaderRegister = i;
        p.Descriptor.RegisterSpace = 0;
        p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = static_cast<UINT>(std::size(params));
    rsd.pParameters = params;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob, error;
    HRESULT hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr)) {
        std::string msg = "D3D12SerializeRootSignature";
        if (error) msg += ": " + std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        throw Dx12Error(hr, msg, false);
    }
    Check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                       IID_PPV_ARGS(&rootSignature_)),
          "CreateRootSignature", device_.Get());

    pipelines_.resize(static_cast<size_t>(ShaderId::Count));
    for (uint32_t i = 0; i < static_cast<uint32_t>(ShaderId::Count); ++i) {
        const ShaderBlob& s = GetShader(static_cast<ShaderId>(i));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rootSignature_.Get();
        pd.CS.pShaderBytecode = s.data;
        pd.CS.BytecodeLength = s.size;
        Check(device_->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipelines_[i])),
              (std::string("CreateComputePipelineState ") + s.name).c_str(), device_.Get());
    }
}

Dx12Buffer Dx12Context::CreateBuffer(uint64_t bytes, HeapKind kind, const wchar_t* debugName)
{
    Dx12Buffer b;
    b.size = AlignUp(bytes ? bytes : 1, 256);
    b.kind = kind;

    D3D12_HEAP_TYPE heap = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_STATES initial = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    switch (kind) {
    case HeapKind::Default:
        flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    case HeapKind::Upload:
        heap = D3D12_HEAP_TYPE_UPLOAD;
        initial = D3D12_RESOURCE_STATE_GENERIC_READ;
        break;
    case HeapKind::Readback:
        heap = D3D12_HEAP_TYPE_READBACK;
        initial = D3D12_RESOURCE_STATE_COPY_DEST;
        break;
    }
    const D3D12_HEAP_PROPERTIES hp = HeapProps(heap);
    const D3D12_RESOURCE_DESC rd = BufferDesc(b.size, flags);
    Check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initial, nullptr,
                                           IID_PPV_ARGS(&b.resource)),
          "CreateCommittedResource", device_.Get());
    if (debugName) b.resource->SetName(debugName);
    b.state = initial;
    b.epoch = epoch_;
    return b;
}

void Dx12Context::BeginList()
{
    if (recording_) return;
    Check(allocator_->Reset(), "CommandAllocator::Reset", device_.Get());
    Check(list_->Reset(allocator_.Get(), nullptr), "CommandList::Reset", device_.Get());
    list_->SetComputeRootSignature(rootSignature_.Get());
    recording_ = true;
    recordedCommands_ = 0;
}

void Dx12Context::Require(Dx12Buffer& b, D3D12_RESOURCE_STATES state)
{
    if (b.kind != HeapKind::Default) return; // upload/readback heaps have fixed states
    // Buffers decay to COMMON when a command list finishes executing.
    if (b.epoch != epoch_) {
        b.state = D3D12_RESOURCE_STATE_COMMON;
        b.epoch = epoch_;
    }
    if (b.state == state) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = b.resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = b.state;
    barrier.Transition.StateAfter = state;
    list_->ResourceBarrier(1, &barrier);
    b.state = state;
}

void Dx12Context::EnsureRingSpace(uint32_t bytes)
{
    const uint64_t needed = AlignUp(bytes, kCbAlign) + AlignUp(frameConstants_.size(), kCbAlign);
    if (ringOffset_ + needed > kRingBytes || recordedCommands_ >= kMaxCommandsPerList) Flush();
}

D3D12_GPU_VIRTUAL_ADDRESS Dx12Context::PushRing(const void* data, uint32_t bytes)
{
    const uint64_t size = AlignUp(bytes ? bytes : 1, kCbAlign);
    if (ringOffset_ + size > kRingBytes) throw Dx12Error(E_OUTOFMEMORY, "Constant ring overflow", false);
    if (bytes) std::memcpy(ringCpu_ + ringOffset_, data, bytes);
    const D3D12_GPU_VIRTUAL_ADDRESS va = ring_.Gpu() + ringOffset_;
    ringOffset_ += size;
    return va;
}

void Dx12Context::SetFrameConstants(const void* data, uint32_t bytes)
{
    frameConstants_.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes);
    frameConstantsDirty_ = true;
}

void Dx12Context::Dispatch(ShaderId shader, const void* constants, uint32_t constantBytes, const Bindings& bindings,
                           uint32_t gx, uint32_t gy, uint32_t gz)
{
    if (gx == 0 || gy == 0 || gz == 0) return;
    EnsureRingSpace(constantBytes);
    BeginList();

    if (frameConstantsDirty_ || frameConstantsGpu_ == 0) {
        frameConstantsGpu_ = frameConstants_.empty()
                                 ? PushRing(nullptr, 0)
                                 : PushRing(frameConstants_.data(), static_cast<uint32_t>(frameConstants_.size()));
        frameConstantsDirty_ = false;
    }
    const D3D12_GPU_VIRTUAL_ADDRESS cb0 = PushRing(constants, constantBytes);

    for (uint32_t i = 0; i < kMaxUavs; ++i)
        if (bindings.uav[i]) Require(*bindings.uav[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Require(dummy_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    list_->SetPipelineState(pipelines_[static_cast<size_t>(shader)].Get());
    list_->SetComputeRootConstantBufferView(0, cb0);
    list_->SetComputeRootConstantBufferView(1, frameConstantsGpu_);
    for (uint32_t i = 0; i < kMaxUavs; ++i) {
        // Unused slots get a small dummy buffer so every root parameter is always set.
        const D3D12_GPU_VIRTUAL_ADDRESS va = bindings.uav[i] ? bindings.uav[i]->Gpu() : dummy_.Gpu();
        list_->SetComputeRootUnorderedAccessView(2 + i, va);
    }
    list_->Dispatch(gx, gy, gz);

    // Every pass depends on the previous one; a global UAV barrier keeps ordering simple.
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = nullptr;
    list_->ResourceBarrier(1, &uavBarrier);
    ++recordedCommands_;
}

void Dx12Context::CopyBuffer(Dx12Buffer& dst, uint64_t dstOffset, Dx12Buffer& src, uint64_t srcOffset, uint64_t bytes)
{
    if (bytes == 0) return;
    if (recordedCommands_ >= kMaxCommandsPerList) Flush();
    BeginList();
    Require(src, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Require(dst, D3D12_RESOURCE_STATE_COPY_DEST);
    list_->CopyBufferRegion(dst.resource.Get(), dstOffset, src.resource.Get(), srcOffset, bytes);
    ++recordedCommands_;
}

void Dx12Context::Readback(Dx12Buffer& src, uint64_t bytes, const std::function<void(const uint8_t*)>& consume)
{
    if (!readback_ || readback_.size < bytes) {
        Flush(); // the old readback buffer may still be referenced by recorded work
        readback_.Reset();
        readback_ = CreateBuffer(bytes, HeapKind::Readback, L"Physarum readback");
    }
    CopyBuffer(readback_, 0, src, 0, bytes);
    Flush();

    D3D12_RANGE readRange{0, static_cast<SIZE_T>(bytes)};
    uint8_t* mapped = nullptr;
    Check(readback_.resource->Map(0, &readRange, reinterpret_cast<void**>(&mapped)), "Map readback", device_.Get());
    try {
        consume(mapped);
    } catch (...) {
        D3D12_RANGE none{0, 0};
        readback_.resource->Unmap(0, &none);
        throw;
    }
    D3D12_RANGE none{0, 0};
    readback_.resource->Unmap(0, &none);
}

void Dx12Context::Flush()
{
    if (recording_) {
        recording_ = false;
        Check(list_->Close(), "CommandList::Close", device_.Get());
        ID3D12CommandList* lists[] = {list_.Get()};
        queue_->ExecuteCommandLists(1, lists);
        Check(queue_->Signal(fence_.Get(), ++fenceValue_), "Queue::Signal", device_.Get());
        if (fence_->GetCompletedValue() < fenceValue_) {
            Check(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_), "SetEventOnCompletion", device_.Get());
            WaitForSingleObject(fenceEvent_, INFINITE);
        }
        DrainDebugMessages();
        // A removed device completes fences with UINT64_MAX; detect that here.
        Check(device_->GetDeviceRemovedReason(), "GPU execution", device_.Get());
        ++epoch_;
    }
    ringOffset_ = 0;
    frameConstantsGpu_ = 0;
    frameConstantsDirty_ = !frameConstants_.empty();
    recordedCommands_ = 0;
}

void Dx12Context::DrainDebugMessages()
{
    if (!infoQueue_) return;
    const UINT64 n = infoQueue_->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        infoQueue_->GetMessage(i, nullptr, &len);
        std::vector<uint8_t> buf(len);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (SUCCEEDED(infoQueue_->GetMessage(i, msg, &len)) && debugMessage_) debugMessage_(msg->pDescription);
    }
    infoQueue_->ClearStoredMessages();
}

uint32_t Dx12Context::Groups(uint64_t items, uint32_t groupSize)
{
    const uint64_t g = (items + groupSize - 1) / groupSize;
    if (g > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION)
        throw Dx12Error(E_INVALIDARG, "Dispatch too large", false);
    return static_cast<uint32_t>(g);
}

} // namespace phys::dx
