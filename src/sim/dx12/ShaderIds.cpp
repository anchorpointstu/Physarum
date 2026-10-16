#include "sim/dx12/ShaderIds.h"

#include <windows.h> // BYTE, used by the fxc-generated headers

#include "shaders/cs_agent_step.h"
#include "shaders/cs_agents_draw.h"
#include "shaders/cs_clear_uint.h"
#include "shaders/cs_present.h"
#include "shaders/cs_spawn.h"
#include "shaders/cs_trail_diffuse.h"
#include "shaders/cs_trail_resolve.h"

namespace phys::dx {

namespace {
#define PHYS_SHADER(sym) ShaderBlob{sym, sizeof(sym), #sym}
const ShaderBlob kShaders[] = {
    PHYS_SHADER(g_cs_clear_uint),
    PHYS_SHADER(g_cs_spawn),
    PHYS_SHADER(g_cs_agent_step),
    PHYS_SHADER(g_cs_trail_resolve),
    PHYS_SHADER(g_cs_trail_diffuse),
    PHYS_SHADER(g_cs_agents_draw),
    PHYS_SHADER(g_cs_present),
};
#undef PHYS_SHADER
static_assert(sizeof(kShaders) / sizeof(kShaders[0]) == static_cast<size_t>(ShaderId::Count));
} // namespace

const ShaderBlob& GetShader(ShaderId id) { return kShaders[static_cast<size_t>(id)]; }

} // namespace phys::dx
