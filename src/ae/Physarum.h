#pragma once

// Common includes and shared declarations for the After Effects host layer.

#include "AEConfig.h"

#include <windows.h>

#include "AEFX_SuiteHandlerTemplate.h"
#include "AEGP_SuiteHandler.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "AE_GeneralPlug.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "entry.h"

#include "PhysarumVersion.h"

#include <cstdint>

namespace phys::ae {

// Global data (one per plugin load), stored in a host handle.
struct GlobalData {
    AEGP_PluginID aegpId;
    A_Boolean isPremiere;
};

// Sequence data (one per effect instance). Flat POD: the host copies it verbatim,
// including into the render-only project copies.
struct SeqData {
    uint32_t magic;
    uint32_t version;
    uint64_t instanceId;
};
constexpr uint32_t kSeqMagic = 0x70535041; // 'APSp'
constexpr uint32_t kSeqVersion = 1;

// Data carried from SMART_PRE_RENDER to SMART_RENDER.
struct PreRenderData {
    int frame;              // simulation frame index being rendered
    PF_State state;         // parameter state over [0, frame + 1) frames
    uint32_t simW;
    uint32_t simH;
    uint32_t agentCount;
    A_long timeStep;
};

// Entry point (exported, name referenced by the PiPL).
extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                                       PF_LayerDef* output, void* extra);

// Implemented in Render.cpp
PF_Err SmartPreRender(PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra);
PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra);
PF_Err LegacyRender(PF_InData* in_data, PF_OutData* out_data, PF_LayerDef* output);

} // namespace phys::ae
