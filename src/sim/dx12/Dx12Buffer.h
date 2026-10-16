#pragma once

#include "sim/dx12/Dx12Common.h"

namespace phys::dx {

enum class HeapKind { Default, Upload, Readback };

// A committed buffer resource. State is tracked by Dx12Context; buffers decay to
// COMMON after every ExecuteCommandLists, which the context models with an epoch.
struct Dx12Buffer {
    ComPtr<ID3D12Resource> resource;
    uint64_t size = 0;
    HeapKind kind = HeapKind::Default;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    uint64_t epoch = 0;

    explicit operator bool() const { return resource != nullptr; }
    D3D12_GPU_VIRTUAL_ADDRESS Gpu() const { return resource ? resource->GetGPUVirtualAddress() : 0; }
    void Reset() { *this = Dx12Buffer{}; }
};

} // namespace phys::dx
