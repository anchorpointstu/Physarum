#include "sim/dx12/Dx12Common.h"

#include <cstdio>

namespace phys::dx {

namespace {
bool IsDeviceLostCode(HRESULT hr)
{
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}
} // namespace

Dx12Error::Dx12Error(HRESULT hr, const std::string& what, bool deviceLost)
    : std::runtime_error(what + " (" + HrToString(hr) + ")"), hr_(hr), deviceLost_(deviceLost)
{
}

std::string HrToString(HRESULT hr)
{
    const char* name = nullptr;
    switch (hr) {
    case E_OUTOFMEMORY: name = "out of memory"; break;
    case E_INVALIDARG: name = "invalid argument"; break;
    case DXGI_ERROR_DEVICE_REMOVED: name = "device removed"; break;
    case DXGI_ERROR_DEVICE_RESET: name = "device reset"; break;
    case DXGI_ERROR_DEVICE_HUNG: name = "device hung"; break;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR: name = "driver internal error"; break;
    case DXGI_ERROR_UNSUPPORTED: name = "unsupported"; break;
    case DXGI_ERROR_NOT_FOUND: name = "no adapter"; break;
    default: break;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return name ? std::string(name) + ", " + buf : std::string(buf);
}

std::string Narrow(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

void Check(HRESULT hr, const char* what, ID3D12Device* device)
{
    if (SUCCEEDED(hr)) return;
    bool lost = IsDeviceLostCode(hr);
    if (device) {
        const HRESULT reason = device->GetDeviceRemovedReason();
        if (FAILED(reason)) {
            lost = true;
            hr = reason;
        }
    }
    throw Dx12Error(hr, what, lost);
}

} // namespace phys::dx
