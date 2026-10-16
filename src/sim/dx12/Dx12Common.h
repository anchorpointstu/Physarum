#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace phys::dx {

using Microsoft::WRL::ComPtr;

// Thrown for any failed Direct3D call. deviceLost() means every GPU object is
// invalid and the context must be recreated.
class Dx12Error : public std::runtime_error {
public:
    Dx12Error(HRESULT hr, const std::string& what, bool deviceLost);
    HRESULT hr() const { return hr_; }
    bool deviceLost() const { return deviceLost_; }

private:
    HRESULT hr_;
    bool deviceLost_;
};

std::string HrToString(HRESULT hr);
std::string Narrow(const std::wstring& w);

// Throws Dx12Error when FAILED(hr). If a device is given, it is queried for a
// removal reason so device loss is reported as such.
void Check(HRESULT hr, const char* what, ID3D12Device* device = nullptr);

inline uint64_t AlignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

} // namespace phys::dx
