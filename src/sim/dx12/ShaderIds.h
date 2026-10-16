#pragma once

#include <cstddef>
#include <cstdint>

namespace phys::dx {

enum class ShaderId : uint32_t {
    ClearUint,
    Spawn,
    AgentStep,
    TrailResolve,
    TrailDiffuse,
    AgentsDraw,
    Present,
    Count
};

struct ShaderBlob {
    const void* data;
    size_t size;
    const char* name;
};

const ShaderBlob& GetShader(ShaderId id);

} // namespace phys::dx
