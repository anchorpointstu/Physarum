// SmartFX pre-render and render: the deterministic time model.
//
// Frame N's image is a pure function of the parameters over frames 0..N. A render
// continues from the live state or the newest valid snapshot at or before N, or
// starts over from frame 0. Cached states are validated by comparing the PF_State of
// the parameters over [0, frame + 1) with the one stored when the state was made.

#include "ae/Params.h"
#include "ae/Physarum.h"
#include "ae/Registry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace phys::ae {

namespace {

constexpr A_long kProgressEvery = 8;

struct TimeInfo {
    A_long step = 0;      // frame duration in layer time
    A_u_long scale = 1;
    int frame = 0;        // frame index of current_time
};

TimeInfo GetTimeInfo(const PF_InData* in_data)
{
    TimeInfo t;
    t.scale = in_data->time_scale ? in_data->time_scale : 1;
    A_long step = in_data->time_step ? in_data->time_step : in_data->local_time_step;
    t.step = step < 0 ? -step : step;
    if (t.step > 0 && in_data->current_time > 0) t.frame = static_cast<int>(in_data->current_time / t.step);
    return t;
}

A_long FrameTime(const TimeInfo& t, int frame)
{
    const int64_t v = int64_t(frame) * t.step;
    return static_cast<A_long>(std::min<int64_t>(v, 0x7FFFFFFF));
}

// Parameter state over the frames [0, frame] (all of time if the frame rate is unknown).
PF_Err StateUpTo(PF_InData* in_data, const TimeInfo& t, int frame, PF_State& state)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    std::memset(&state, 0, sizeof(state));
    if (t.step <= 0)
        return suites.ParamUtilsSuite3()->PF_GetCurrentState(in_data->effect_ref, PF_ParamIndex_CHECK_ALL_HONOR_EXCLUDE,
                                                             nullptr, nullptr, &state);
    const A_Time start{0, t.scale};
    const A_Time duration{FrameTime(t, frame + 1), t.scale};
    return suites.ParamUtilsSuite3()->PF_GetCurrentState(in_data->effect_ref, PF_ParamIndex_CHECK_ALL_HONOR_EXCLUDE,
                                                         &start, &duration, &state);
}

bool SameState(PF_InData* in_data, const PF_State& a, const StateTag& tag)
{
    static_assert(sizeof(PF_State) == sizeof(StateTag));
    PF_State b;
    std::memcpy(&b, tag.data(), sizeof(b));
    A_Boolean same = FALSE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    if (suites.ParamUtilsSuite3()->PF_AreStatesIdentical(in_data->effect_ref, &a, &b, &same)) return false;
    return same != FALSE;
}

StateTag ToTag(const PF_State& s)
{
    StateTag t;
    std::memcpy(t.data(), &s, sizeof(s));
    return t;
}

double Ratio(const PF_RationalScale& r) { return r.den ? double(r.num) / double(r.den) : 1.0; }

PF_Err GetPixelFormat(PF_InData* in_data, PF_EffectWorld* world, PixelFormat& out)
{
    AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2);
    PF_PixelFormat fmt = PF_PixelFormat_INVALID;
    const PF_Err err = ws->PF_GetPixelFormat(world, &fmt);
    if (err) return err;
    switch (fmt) {
    case PF_PixelFormat_ARGB32: out = PixelFormat::U8; return PF_Err_NONE;
    case PF_PixelFormat_ARGB64: out = PixelFormat::U16; return PF_Err_NONE;
    case PF_PixelFormat_ARGB128: out = PixelFormat::F32; return PF_Err_NONE;
    default: return PF_Err_BAD_CALLBACK_PARAM;
    }
}

// CPU fill used when the GPU is unavailable or an error occurred.
void FillWorld(PF_EffectWorld* w, PixelFormat fmt, const float argb[4])
{
    if (!w || !w->data) return;
    for (A_long y = 0; y < w->height; ++y) {
        uint8_t* row = reinterpret_cast<uint8_t*>(w->data) + int64_t(y) * w->rowbytes;
        for (A_long x = 0; x < w->width; ++x) {
            if (fmt == PixelFormat::U8) {
                PF_Pixel8* p = reinterpret_cast<PF_Pixel8*>(row) + x;
                p->alpha = A_u_char(argb[0] * 255.0f + 0.5f);
                p->red = A_u_char(argb[1] * 255.0f + 0.5f);
                p->green = A_u_char(argb[2] * 255.0f + 0.5f);
                p->blue = A_u_char(argb[3] * 255.0f + 0.5f);
            } else if (fmt == PixelFormat::U16) {
                PF_Pixel16* p = reinterpret_cast<PF_Pixel16*>(row) + x;
                p->alpha = A_u_short(argb[0] * PF_MAX_CHAN16 + 0.5f);
                p->red = A_u_short(argb[1] * PF_MAX_CHAN16 + 0.5f);
                p->green = A_u_short(argb[2] * PF_MAX_CHAN16 + 0.5f);
                p->blue = A_u_short(argb[3] * PF_MAX_CHAN16 + 0.5f);
            } else {
                PF_PixelFloat* p = reinterpret_cast<PF_PixelFloat*>(row) + x;
                p->alpha = argb[0];
                p->red = argb[1];
                p->green = argb[2];
                p->blue = argb[3];
            }
        }
    }
}

void ReportOnce(PF_OutData* out_data, Instance* inst, const std::string& message)
{
    if (inst && inst->errorShown) return;
    if (inst) inst->errorShown = true;
    std::snprintf(out_data->return_msg, sizeof(out_data->return_msg), "Physarum: %s", message.c_str());
    out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
}

uint64_t InstanceIdOf(PF_InData* in_data)
{
    PF_ConstHandle h = in_data->sequence_data;
    if (!h) {
        AEFX_SuiteScoper<PF_EffectSequenceDataSuite1, true> sds(in_data, kPFEffectSequenceDataSuite,
                                                                kPFEffectSequenceDataSuiteVersion1);
        if (sds.get()) sds->PF_GetConstSequenceData(in_data->effect_ref, &h);
    }
    if (!h || !*h) return 0;
    const SeqData* sd = reinterpret_cast<const SeqData*>(*h);
    return sd->magic == kSeqMagic ? sd->instanceId : 0;
}

} // namespace

// ---------------------------------------------------------------- pre-render

PF_Err SmartPreRender(PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra)
{
    (void)out_data;
    const TimeInfo t = GetTimeInfo(in_data);

    StructuralParams st;
    PF_Err err = ReadStructural(in_data, in_data->current_time, t.step, t.scale, st);
    if (err) return err;

    // Background (animatable) decides whether the output is opaque.
    PF_ParamDef bg;
    AEFX_CLR_STRUCT(bg);
    err = PF_CHECKOUT_PARAM(in_data, P_BACKGROUND, in_data->current_time, t.step, t.scale, &bg);
    if (err) return err;
    const bool opaque = bg.u.pd.value != A_long(Background::Transparent) + 1;
    PF_CHECKIN_PARAM(in_data, &bg);

    auto* pd = new PreRenderData{};
    pd->frame = t.frame;
    pd->simW = st.simW;
    pd->simH = st.simH;
    pd->agentCount = st.agentCount;
    pd->timeStep = t.step;
    err = StateUpTo(in_data, t, t.frame, pd->state);
    if (err) {
        delete pd;
        return err;
    }

    // Everything that decides the pixels and is not a plain parameter value at this
    // time goes into the cache key.
    struct {
        PF_State state;
        int32_t frame;
        uint32_t simW, simH, agents, version;
    } mix{pd->state, pd->frame, pd->simW, pd->simH, pd->agentCount, PHYSARUM_PIPL_VERSION};
    err = extra->cb->GuidMixInPtr(in_data->effect_ref, sizeof(mix), &mix);
    if (err) {
        delete pd;
        return err;
    }

    // The effect fills the whole layer, in downsampled layer coordinates.
    const double dsx = Ratio(in_data->downsample_x);
    const double dsy = Ratio(in_data->downsample_y);
    PF_LRect full;
    full.left = 0;
    full.top = 0;
    full.right = static_cast<A_long>(std::ceil(in_data->width * dsx));
    full.bottom = static_cast<A_long>(std::ceil(in_data->height * dsy));

    const PF_LRect& req = extra->input->output_request.rect;
    PF_LRect res;
    res.left = std::max(req.left, full.left);
    res.top = std::max(req.top, full.top);
    res.right = std::min(req.right, full.right);
    res.bottom = std::min(req.bottom, full.bottom);
    if (res.right < res.left) res.right = res.left;
    if (res.bottom < res.top) res.bottom = res.top;

    extra->output->max_result_rect = full;
    extra->output->result_rect = res;
    extra->output->solid = opaque ? TRUE : FALSE;
    extra->output->flags = 0;
    extra->output->pre_render_data = pd;
    extra->output->delete_pre_render_data_func = [](void* p) { delete static_cast<PreRenderData*>(p); };
    return PF_Err_NONE;
}

// ---------------------------------------------------------------- render

PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra)
{
    PF_EffectWorld* out = nullptr;
    PF_Err err = extra->cb->checkout_output(in_data->effect_ref, &out);
    if (err || !out) return err;
    PixelFormat fmt = PixelFormat::U8;
    err = GetPixelFormat(in_data, out, fmt);
    if (err) return err;

    const float clear[4] = {0, 0, 0, 0};
    const auto* pd = static_cast<const PreRenderData*>(extra->input->pre_render_data);
    const GlobalData* g = in_data->global_data ? reinterpret_cast<const GlobalData*>(DH(in_data->global_data)) : nullptr;
    if (!pd || (g && g->isPremiere)) {
        FillWorld(out, fmt, clear);
        if (g && g->isPremiere) ReportOnce(out_data, nullptr, "Premiere Pro is not supported; use After Effects.");
        return PF_Err_NONE;
    }

    const uint64_t id = InstanceIdOf(in_data);
    std::shared_ptr<Instance> inst = Registry::Get().Acquire(id);
    Registry& reg = Registry::Get();
    const TimeInfo t = GetTimeInfo(in_data);
    const int target = pd->frame;

    StructuralParams st;
    err = ReadStructural(in_data, in_data->current_time, t.step, t.scale, st);
    if (err) return err;
    st.simW = pd->simW; // keep exactly what pre-render mixed into the cache key
    st.simH = pd->simH;
    st.agentCount = pd->agentCount;

    // Full parameters at the current time: structural values, colours, present settings.
    SimParams present;
    err = ReadSimParams(in_data, in_data->current_time, t.step, t.scale, st, present);
    if (err) return err;

    // Fallback colour if anything goes wrong below.
    float fallback[4] = {0, 0, 0, 0};
    if (present.background == uint32_t(Background::Black)) fallback[0] = 1.0f;
    if (present.background == uint32_t(Background::Colour)) {
        fallback[0] = 1.0f;
        fallback[1] = present.bgColour[0];
        fallback[2] = present.bgColour[1];
        fallback[3] = present.bgColour[2];
    }

    try {
        // ---- device + geometry, and the candidate starting points (GPU lock, no host calls)
        int liveFrame = -1;
        StateTag liveTag{};
        std::vector<int> snapFrames;
        std::vector<StateTag> snapTags;
        {
            std::lock_guard<std::mutex> gpu(reg.GpuMutex());
            std::string error;
            dx::Dx12Context* ctx = reg.EnsureDevice(error);
            if (!ctx) {
                FillWorld(out, fmt, fallback);
                ReportOnce(out_data, inst.get(),
                           "could not create a Direct3D 12 device (" + error +
                               "). The simulation needs a DirectX 12 capable GPU and driver.");
                return PF_Err_NONE;
            }
            if (!inst->sim) inst->sim = std::make_unique<SimInstance>(*ctx);
            inst->sim->Configure(Geometry{st.simW, st.simH, st.agentCount});
            liveFrame = inst->sim->LiveFrame();
            liveTag = inst->sim->LiveTag();
            for (int f : inst->sim->SnapshotFrames()) {
                if (f > target) break;
                snapFrames.push_back(f);
                snapTags.push_back(*inst->sim->SnapshotTag(f));
            }
        }

        // ---- validate candidates against the current parameters (host calls, no locks)
        int restoreFrom = -1;     // snapshot to restore, or -1
        int dropFrom = -1;        // snapshots at or after this frame are stale
        for (size_t i = snapFrames.size(); i-- > 0;) {
            PF_State now;
            if ((err = StateUpTo(in_data, t, snapFrames[i], now)) != PF_Err_NONE) return err;
            if (SameState(in_data, now, snapTags[i])) {
                restoreFrom = snapFrames[i];
                break;
            }
            dropFrom = snapFrames[i];
        }
        bool useLive = false;
        if (liveFrame >= 0 && liveFrame <= target && liveFrame > restoreFrom) {
            PF_State now;
            if ((err = StateUpTo(in_data, t, liveFrame, now)) != PF_Err_NONE) return err;
            useLive = SameState(in_data, now, liveTag);
        }

        // ---- reset if there is nothing to continue from
        SimParams base = present;
        int frame = 0;
        {
            std::lock_guard<std::mutex> gpu(reg.GpuMutex());
            SimInstance& sim = *inst->sim;
            if (dropFrom >= 0) sim.DropSnapshotsFrom(dropFrom);
            if (useLive) {
                frame = sim.LiveFrame();
            } else if (restoreFrom >= 0) {
                sim.RestoreSnapshot(restoreFrom);
                frame = restoreFrom;
            } else {
                frame = -1; // needs a reset with frame-0 parameters
            }
        }
        if (frame < 0) {
            SimParams p0;
            if ((err = ReadSimParams(in_data, 0, t.step, t.scale, st, p0)) != PF_Err_NONE) return err;
            PF_State s0;
            if ((err = StateUpTo(in_data, t, 0, s0)) != PF_Err_NONE) return err;
            std::lock_guard<std::mutex> gpu(reg.GpuMutex());
            inst->sim->Reset(p0);
            inst->sim->SetLiveTag(ToTag(s0));
            inst->sim->TakeSnapshot(ToTag(s0), st.cacheBytes);
            frame = 0;
            base = p0;
        }

        // ---- step forward to the target frame
        const int first = frame;
        for (int f = frame + 1; f <= target; ++f) {
            SimParams p;
            if ((err = ReadStepParams(in_data, FrameTime(t, f), t.step, t.scale, base, p)) != PF_Err_NONE) return err;
            {
                std::lock_guard<std::mutex> gpu(reg.GpuMutex());
                inst->sim->StepFrame(p);
            }
            const bool snapshot = st.snapshotEvery > 0 && f % int(st.snapshotEvery) == 0;
            const bool progress = (f - first) % kProgressEvery == 0 || f == target;
            if (snapshot || (progress && f != target)) {
                PF_State s;
                if ((err = StateUpTo(in_data, t, f, s)) != PF_Err_NONE) return err;
                std::lock_guard<std::mutex> gpu(reg.GpuMutex());
                inst->sim->SetLiveTag(ToTag(s));
                if (snapshot) inst->sim->TakeSnapshot(ToTag(s), st.cacheBytes);
            }
            if (progress && target > first) {
                // Returns an interrupt error if the user cancelled; the live state stays valid.
                if ((err = PF_PROGRESS(in_data, f - first, target - first)) != PF_Err_NONE) return err;
            }
        }

        // ---- present
        OutputDesc od;
        od.width = static_cast<uint32_t>(out->width);
        od.height = static_cast<uint32_t>(out->height);
        od.rowBytes = static_cast<uint32_t>(out->rowbytes);
        od.format = fmt;
        od.originX = static_cast<float>(out->origin_x);
        od.originY = static_cast<float>(out->origin_y);
        const double dsx = Ratio(in_data->downsample_x);
        const double dsy = Ratio(in_data->downsample_y);
        od.scaleX = static_cast<float>(double(st.simW) / (std::max<A_long>(in_data->width, 1) * (dsx > 0 ? dsx : 1.0)));
        od.scaleY = static_cast<float>(double(st.simH) / (std::max<A_long>(in_data->height, 1) * (dsy > 0 ? dsy : 1.0)));

        std::lock_guard<std::mutex> gpu(reg.GpuMutex());
        inst->sim->SetLiveTag(ToTag(pd->state)); // pre-render state covers [0, target]
        inst->sim->Present(present, od, out->data);
        inst->errorShown = false;
        inst.reset(); // release under the GPU lock
    } catch (const dx::Dx12Error& e) {
        std::lock_guard<std::mutex> gpu(reg.GpuMutex());
        if (e.deviceLost()) {
            reg.DropDevice();
        } else if (inst->sim) {
            try {
                inst->sim->DropAll();
            } catch (...) {
                inst->sim.reset();
            }
        }
        FillWorld(out, fmt, fallback);
        std::string msg = e.what();
        if (e.hr() == E_OUTOFMEMORY)
            msg += ". Out of GPU memory: lower Simulation Scale, Agent Count or Cache Limit.";
        else if (e.deviceLost())
            msg += ". The GPU was reset; render again to continue.";
        ReportOnce(out_data, inst.get(), msg);
        inst.reset();
    }
    return PF_Err_NONE;
}

// Non-SmartFX hosts (Premiere Pro) only get a transparent frame.
PF_Err LegacyRender(PF_InData* in_data, PF_OutData* out_data, PF_LayerDef* output)
{
    (void)in_data;
    if (output && output->data) {
        for (A_long y = 0; y < output->height; ++y)
            std::memset(reinterpret_cast<uint8_t*>(output->data) + int64_t(y) * output->rowbytes, 0,
                        size_t(output->width) * (PF_WORLD_IS_DEEP(output) ? 8 : 4));
    }
    ReportOnce(out_data, nullptr, "this host is not supported; use After Effects.");
    return PF_Err_NONE;
}

} // namespace phys::ae
