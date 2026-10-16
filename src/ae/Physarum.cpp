// Entry points and command dispatch for the Physarum After Effects effect.

#include "ae/Params.h"
#include "ae/Physarum.h"
#include "ae/Registry.h"

#include <cstdio>
#include <cstring>

namespace phys::ae {

// The PiPL is generated from the same constants; these keep them honest.
static_assert(PHYSARUM_OUTFLAGS == (PF_OutFlag_WIDE_TIME_INPUT | PF_OutFlag_NON_PARAM_VARY | PF_OutFlag_DEEP_COLOR_AWARE |
                                    PF_OutFlag_SEND_UPDATE_PARAMS_UI),
              "PHYSARUM_OUTFLAGS does not match the flags set in GlobalSetup");
static_assert(PHYSARUM_OUTFLAGS2 == (PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG | PF_OutFlag2_SUPPORTS_SMART_RENDER |
                                     PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT |
                                     PF_OutFlag2_I_MIX_GUID_DEPENDENCIES),
              "PHYSARUM_OUTFLAGS2 does not match the flags set in GlobalSetup");
static_assert(PHYSARUM_PIPL_VERSION ==
                  PF_VERSION(PHYSARUM_MAJOR, PHYSARUM_MINOR, PHYSARUM_BUG, PF_Stage_DEVELOP, PHYSARUM_BUILD),
              "PHYSARUM_PIPL_VERSION does not match PF_VERSION(...)");

namespace {

PF_Err About(PF_InData* in_data, PF_OutData* out_data)
{
    (void)in_data;
    std::snprintf(out_data->return_msg, sizeof(out_data->return_msg),
                  "%s %d.%d.%d\rGPU Physarum (slime mould) simulation.\rDirectX 12, deterministic from frame 0.",
                  PHYSARUM_NAME, PHYSARUM_MAJOR, PHYSARUM_MINOR, PHYSARUM_BUG);
    return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data)
{
    out_data->my_version = PF_VERSION(PHYSARUM_MAJOR, PHYSARUM_MINOR, PHYSARUM_BUG, PF_Stage_DEVELOP, PHYSARUM_BUILD);
    out_data->out_flags = PHYSARUM_OUTFLAGS;
    out_data->out_flags2 = PHYSARUM_OUTFLAGS2;

    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Handle h = suites.HandleSuite1()->host_new_handle(sizeof(GlobalData));
    if (!h) return PF_Err_OUT_OF_MEMORY;
    GlobalData* g = reinterpret_cast<GlobalData*>(suites.HandleSuite1()->host_lock_handle(h));
    if (!g) return PF_Err_OUT_OF_MEMORY;
    std::memset(g, 0, sizeof(*g));
    g->isPremiere = in_data->appl_id == kAppID_Premiere;
    if (!g->isPremiere) suites.UtilitySuite6()->AEGP_RegisterWithAEGP(nullptr, PHYSARUM_NAME, &g->aegpId);
    suites.HandleSuite1()->host_unlock_handle(h);
    out_data->global_data = h;
    return PF_Err_NONE;
}

PF_Err GlobalSetdown(PF_InData* in_data)
{
    Registry::Get().Shutdown();
    if (in_data->global_data) {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        suites.HandleSuite1()->host_dispose_handle(in_data->global_data);
    }
    return PF_Err_NONE;
}

PF_Err NewSequenceHandle(PF_InData* in_data, PF_OutData* out_data, uint64_t id)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Handle h = suites.HandleSuite1()->host_new_handle(sizeof(SeqData));
    if (!h) return PF_Err_OUT_OF_MEMORY;
    SeqData* sd = reinterpret_cast<SeqData*>(suites.HandleSuite1()->host_lock_handle(h));
    if (!sd) return PF_Err_OUT_OF_MEMORY;
    sd->magic = kSeqMagic;
    sd->version = kSeqVersion;
    sd->instanceId = id;
    suites.HandleSuite1()->host_unlock_handle(h);
    out_data->sequence_data = h;
    Registry::Get().OnSetup(h, id);
    return PF_Err_NONE;
}

PF_Err SequenceSetup(PF_InData* in_data, PF_OutData* out_data)
{
    return NewSequenceHandle(in_data, out_data, Registry::Get().NewId());
}

PF_Err SequenceResetup(PF_InData* in_data, PF_OutData* out_data)
{
    PF_Handle h = in_data->sequence_data;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    SeqData* sd = nullptr;
    if (h && suites.HandleSuite1()->host_get_handle_size(h) >= sizeof(SeqData))
        sd = reinterpret_cast<SeqData*>(suites.HandleSuite1()->host_lock_handle(h));
    if (!sd || sd->magic != kSeqMagic || sd->version != kSeqVersion) {
        if (sd) suites.HandleSuite1()->host_unlock_handle(h);
        if (h) suites.HandleSuite1()->host_dispose_handle(h);
        return SequenceSetup(in_data, out_data);
    }
    const bool renderOnly = (in_data->in_flags & PF_InFlag_PROJECT_IS_RENDER_ONLY) != 0;
    sd->instanceId = Registry::Get().OnResetup(h, sd->instanceId, renderOnly);
    suites.HandleSuite1()->host_unlock_handle(h);
    out_data->sequence_data = h;
    return PF_Err_NONE;
}

PF_Err SequenceSetdown(PF_InData* in_data)
{
    if (!in_data->sequence_data) return PF_Err_NONE;
    Registry::Get().OnSetdown(in_data->sequence_data);
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.HandleSuite1()->host_dispose_handle(in_data->sequence_data);
    return PF_Err_NONE;
}

} // namespace

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr,
                                                     SPBasicSuite* inSPBasicSuitePtr, const char* inHostName,
                                                     const char* inHostVersion)
{
    (void)inSPBasicSuitePtr;
    (void)inHostName;
    (void)inHostVersion;
    PF_Err result = PF_Err_INVALID_CALLBACK;
    result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr, PHYSARUM_NAME, PHYSARUM_MATCH_NAME,
                                     PHYSARUM_CATEGORY, AE_RESERVED_INFO, "EffectMain", PHYSARUM_SUPPORT_URL);
    return result;
}

extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                                       PF_LayerDef* output, void* extra)
{
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
        case PF_Cmd_ABOUT: err = About(in_data, out_data); break;
        case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data); break;
        case PF_Cmd_GLOBAL_SETDOWN: err = GlobalSetdown(in_data); break;
        case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data); break;
        case PF_Cmd_SEQUENCE_SETUP: err = SequenceSetup(in_data, out_data); break;
        case PF_Cmd_SEQUENCE_RESETUP: err = SequenceResetup(in_data, out_data); break;
        case PF_Cmd_SEQUENCE_SETDOWN: err = SequenceSetdown(in_data); break;
        case PF_Cmd_SEQUENCE_FLATTEN: break; // sequence data is already flat
        case PF_Cmd_USER_CHANGED_PARAM:
            err = UserChangedParam(in_data, out_data, params, reinterpret_cast<const PF_UserChangedParamExtra*>(extra));
            break;
        case PF_Cmd_UPDATE_PARAMS_UI: err = UpdateParamsUI(in_data, out_data, params); break;
        case PF_Cmd_SMART_PRE_RENDER:
            err = SmartPreRender(in_data, out_data, reinterpret_cast<PF_PreRenderExtra*>(extra));
            break;
        case PF_Cmd_SMART_RENDER:
            err = SmartRender(in_data, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra));
            break;
        case PF_Cmd_RENDER: err = LegacyRender(in_data, out_data, output); break;
        default: break;
        }
    } catch (const PF_Err& thrown) {
        err = thrown;
    } catch (...) {
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    return err;
}

} // namespace phys::ae
