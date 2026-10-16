#include "ae/Params.h"

#include "sim/Presets.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace phys::ae {

namespace {

constexpr float kDeg = 3.14159265358979f / 180.0f;
constexpr int kSnapshotFlags = PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_EXCLUDE_FROM_HAVE_INPUTS_CHANGED;

const char* PresetPopupString()
{
    static std::string s = [] {
        std::string r = "Custom";
        size_t n = 0;
        const Preset* p = GetPresets(n);
        for (size_t i = 0; i < n; ++i) r += std::string("|") + p[i].name;
        return r;
    }();
    return s.c_str();
}

A_short PresetPopupCount()
{
    size_t n = 0;
    GetPresets(n);
    return static_cast<A_short>(n + 1);
}

// Parameters a preset sets. Editing any of them by hand switches Preset to Custom.
bool IsPresetControlled(PF_ParamIndex idx)
{
    switch (idx) {
    case P_SPECIES:
    case P_BOUNDARY:
    case P_DECAY:
    case P_DIFF_RADIUS:
    case P_DIFF_STRENGTH:
    case P_TRAIL_CLAMP:
        return true;
    default:
        break;
    }
    if (idx >= P_SPECIES_BASE && idx < P_COLOUR_TOPIC) {
        switch ((idx - P_SPECIES_BASE) % SPECIES_STRIDE) {
        case SF_SENSOR_ANGLE:
        case SF_SENSOR_DIST:
        case SF_TURN_ANGLE:
        case SF_STEP_SIZE:
        case SF_DEPOSIT:
        case SF_JITTER:
        case SF_OTHER:
            return true;
        default:
            break;
        }
    }
    return false;
}

PF_Err AddSpecies(PF_InData* in_data, uint32_t s)
{
    PF_ParamDef def;
    const int base = D_SPECIES_BASE + 20 * int(s);
    static const PF_Pixel kColours[kMaxSpecies] = {
        {255, 255, 255, 255}, {255, 0, 255, 255}, {255, 255, 0, 255}, {255, 255, 255, 0}}; // ARGB
    const char* names[kMaxSpecies] = {"Species 1", "Species 2", "Species 3", "Species 4"};
    const int supervise = PF_ParamFlag_SUPERVISE;
    const int structural = PF_ParamFlag_CANNOT_TIME_VARY;

    AEFX_CLR_STRUCT(def);
    def.flags = s == 0 ? 0 : PF_ParamFlag_START_COLLAPSED;
    PF_ADD_TOPIC(names[s], base + SF_TOPIC);

    PF_ADD_FLOAT_SLIDERX("Share", 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, structural,
                         base + SF_SHARE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Color", kColours[s].red, kColours[s].green, kColours[s].blue, base + SF_COLOUR);
    PF_ADD_FLOAT_SLIDERX("Sensor Angle", 0, 180, 0, 90, 22.5, PF_Precision_TENTHS, 0, supervise,
                         base + SF_SENSOR_ANGLE);
    PF_ADD_FLOAT_SLIDERX("Sensor Distance", 0, 100, 0, 50, 9, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL,
                         supervise, base + SF_SENSOR_DIST);
    PF_ADD_FLOAT_SLIDERX("Turn Angle", 0, 180, 0, 90, 45, PF_Precision_TENTHS, 0, supervise, base + SF_TURN_ANGLE);
    PF_ADD_FLOAT_SLIDERX("Step Size", 0, 10, 0, 5, 1, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_PIXEL, supervise,
                         base + SF_STEP_SIZE);
    PF_ADD_FLOAT_SLIDERX("Deposit", 0, 5, 0, 5, 1, PF_Precision_HUNDREDTHS, 0, supervise, base + SF_DEPOSIT);
    PF_ADD_FLOAT_SLIDERX("Wander", 0, 180, 0, 45, 0, PF_Precision_TENTHS, 0, supervise, base + SF_JITTER);
    PF_ADD_FLOAT_SLIDERX("Other Species", -100, 100, -100, 100, -50, PF_Precision_INTEGER,
                         PF_ValueDisplayFlag_PERCENT, supervise, base + SF_OTHER);
    PF_ADD_POPUPX("Spawn Pattern", 5, 1, "Random|Center|Ring|Box|Edge", structural, base + SF_SPAWN);
    AEFX_CLR_STRUCT(def);
    def.flags = structural;
    PF_ADD_POINT("Spawn Center", 50, 50, FALSE, base + SF_SPAWN_CENTRE);
    PF_ADD_FLOAT_SLIDERX("Spawn Size", 0, 200, 0, 100, 50, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT,
                         structural, base + SF_SPAWN_SIZE);
    PF_ADD_POPUPX("Heading", 3, 1, "Random|Inward|Outward", structural, base + SF_HEADING);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(base + SF_TOPIC_END);
    return PF_Err_NONE;
}

// Owns a set of parameter checkouts and checks them all back in.
class Checkouts {
public:
    Checkouts(PF_InData* in_data, A_long time, A_long step, A_u_long scale)
        : in_data_(in_data), time_(time), step_(step), scale_(scale)
    {
    }
    ~Checkouts()
    {
        for (int i = 0; i < P_COUNT; ++i)
            if (out_[i]) PF_CHECKIN_PARAM(in_data_, &defs_[i]);
    }
    const PF_ParamDef* Get(int idx, PF_Err& err)
    {
        if (err) return nullptr;
        if (!out_[idx]) {
            AEFX_CLR_STRUCT(defs_[idx]);
            err = PF_CHECKOUT_PARAM(in_data_, idx, time_, step_, scale_, &defs_[idx]);
            if (err) return nullptr;
            out_[idx] = true;
        }
        return &defs_[idx];
    }
    float Float(int idx, PF_Err& err)
    {
        const PF_ParamDef* d = Get(idx, err);
        return d ? static_cast<float>(d->u.fs_d.value) : 0.0f;
    }
    uint32_t Popup(int idx, PF_Err& err) // 0-based
    {
        const PF_ParamDef* d = Get(idx, err);
        return d ? static_cast<uint32_t>(std::max<A_long>(d->u.pd.value - 1, 0)) : 0u;
    }
    bool Check(int idx, PF_Err& err)
    {
        const PF_ParamDef* d = Get(idx, err);
        return d && d->u.bd.value;
    }
    void Colour(int idx, float out[4], PF_Err& err)
    {
        const PF_ParamDef* d = Get(idx, err);
        if (!d) return;
        out[0] = d->u.cd.value.red / 255.0f;
        out[1] = d->u.cd.value.green / 255.0f;
        out[2] = d->u.cd.value.blue / 255.0f;
        out[3] = 1.0f;
    }

private:
    PF_InData* in_data_;
    A_long time_;
    A_long step_;
    A_u_long scale_;
    PF_ParamDef defs_[P_COUNT];
    bool out_[P_COUNT] = {};
};

double Ratio(const PF_RationalScale& r) { return r.den ? double(r.num) / double(r.den) : 1.0; }

// Spawn centres are quantised to 0.01 % of the frame. Point values arrive scaled by
// the preview downsample factor in 16.16 fixed point; quantising makes full and
// reduced resolution produce exactly the same simulation.
float QuantiseFraction(double f) { return static_cast<float>(std::round(f * 10000.0) / 10000.0); }

} // namespace

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data)
{
    PF_ParamDef def;
    const int supervise = PF_ParamFlag_SUPERVISE;
    const int structural = PF_ParamFlag_CANNOT_TIME_VARY;

    PF_ADD_POPUPX("Preset", PresetPopupCount(), kPresetCustom, PresetPopupString(), supervise | structural, D_PRESET);
    PF_ADD_POPUPX("Species", 4, 1, "1|2|3|4", supervise | structural, D_SPECIES);
    PF_ADD_FLOAT_SLIDERX("Seed", 0, 99999, 0, 1000, 1, PF_Precision_INTEGER, 0, structural, D_SEED);
    PF_ADD_FLOAT_SLIDERX("Agent Count", 1000, kMaxAgents, 1000, 2000000, 500000, PF_Precision_INTEGER, 0, structural,
                         D_AGENTS);
    PF_ADD_FLOAT_SLIDERX("Simulation Scale", 10, 200, 10, 200, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT,
                         structural, D_SIM_SCALE);
    PF_ADD_FLOAT_SLIDERX("Steps Per Frame", 1, 10, 1, 10, 1, PF_Precision_INTEGER, 0, 0, D_STEPS);
    PF_ADD_FLOAT_SLIDERX("Pre-roll Steps", 0, 5000, 0, 1000, 0, PF_Precision_INTEGER, 0, structural, D_PREROLL);
    PF_ADD_POPUPX("Boundary", 3, 1, "Wrap|Bounce|Respawn", supervise, D_BOUNDARY);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Trail", D_TRAIL_TOPIC);
    PF_ADD_FLOAT_SLIDERX("Decay", 0, 100, 0, 50, 10, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, supervise,
                         D_DECAY);
    PF_ADD_FLOAT_SLIDERX("Diffusion Radius", 0, 2, 0, 2, 1, PF_Precision_INTEGER, PF_ValueDisplayFlag_PIXEL, supervise,
                         D_DIFF_RADIUS);
    PF_ADD_FLOAT_SLIDERX("Diffusion Strength", 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT,
                         supervise, D_DIFF_STRENGTH);
    PF_ADD_FLOAT_SLIDERX("Trail Ceiling", 0.1, 100, 1, 50, 10, PF_Precision_HUNDREDTHS, 0, supervise, D_TRAIL_CLAMP);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(D_TRAIL_END);

    for (uint32_t s = 0; s < kMaxSpecies; ++s) {
        const PF_Err err = AddSpecies(in_data, s);
        if (err) return err;
    }

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Color", D_COLOUR_TOPIC);
    PF_ADD_POPUPX("Palette", 7, 1, "Species Colors|Gradient|Fire|Ice|Neon|Mono|Bio", supervise, D_PALETTE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Gradient Low", 0, 0, 0, D_COL_A);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Gradient Mid", 255, 115, 13, D_COL_B);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Gradient High", 255, 255, 255, D_COL_C);
    PF_ADD_FLOAT_SLIDERX("Exposure", 0, 10, 0, 4, 1, PF_Precision_HUNDREDTHS, 0, 0, D_EXPOSURE);
    PF_ADD_FLOAT_SLIDERX("Gamma", 0.1, 5, 0.2, 3, 1, PF_Precision_HUNDREDTHS, 0, 0, D_GAMMA);
    PF_ADD_CHECKBOXX("Alpha From Brightness", TRUE, 0, D_ALPHA_FROM_INTENSITY);
    PF_ADD_POPUPX("Background", 3, 1, "Transparent|Black|Color", supervise, D_BACKGROUND);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Background Color", 0, 0, 0, D_BG_COLOUR);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(D_COLOUR_END);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_START_COLLAPSED;
    PF_ADD_TOPIC("Agents", D_AGENTS_TOPIC);
    PF_ADD_CHECKBOXX("Draw Agents", FALSE, 0, D_DRAW_AGENTS);
    PF_ADD_FLOAT_SLIDERX("Agent Size", 1, 4, 1, 4, 1, PF_Precision_INTEGER, PF_ValueDisplayFlag_PIXEL, 0, D_AGENT_SIZE);
    PF_ADD_FLOAT_SLIDERX("Agent Opacity", 0, 100, 0, 100, 50, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, 0,
                         D_AGENT_OPACITY);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(D_AGENTS_END);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_START_COLLAPSED;
    PF_ADD_TOPIC("Cache", D_CACHE_TOPIC);
    PF_ADD_FLOAT_SLIDERX("Snapshot Every", 1, 120, 1, 120, 24, PF_Precision_INTEGER, 0, kSnapshotFlags,
                         D_SNAPSHOT_EVERY);
    PF_ADD_FLOAT_SLIDERX("Cache Limit (MB)", 64, 4096, 64, 2048, 512, PF_Precision_INTEGER, 0, kSnapshotFlags,
                         D_CACHE_MB);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(D_CACHE_END);

    out_data->num_params = P_COUNT;
    return PF_Err_NONE;
}

PF_Err ReadStructural(PF_InData* in_data, A_long time, A_long step, A_u_long scale, StructuralParams& out)
{
    PF_Err err = PF_Err_NONE;
    Checkouts c(in_data, time, step, scale);
    out.numSpecies = std::clamp<uint32_t>(c.Popup(P_SPECIES, err) + 1, 1, kMaxSpecies);
    out.agentCount = static_cast<uint32_t>(std::clamp<float>(std::round(c.Float(P_AGENTS, err)), 1.0f, float(kMaxAgents)));
    const double simScale = std::clamp<double>(c.Float(P_SIM_SCALE, err), 10.0, 200.0) / 100.0;
    out.snapshotEvery = static_cast<uint32_t>(std::clamp<float>(std::round(c.Float(P_SNAPSHOT_EVERY, err)), 1.0f, 120.0f));
    out.cacheBytes = static_cast<uint64_t>(std::clamp<float>(c.Float(P_CACHE_MB, err), 64.0f, 4096.0f)) << 20;
    if (err) return err;

    // Simulation size: the layer's full-resolution size times the scale, capped.
    double w = std::max<A_long>(in_data->width, 1) * simScale;
    double h = std::max<A_long>(in_data->height, 1) * simScale;
    const double side = std::max(w, h);
    if (side > kMaxSimSide) {
        w *= kMaxSimSide / side;
        h *= kMaxSimSide / side;
    }
    if (w * h > double(kMaxSimTexels)) {
        const double f = std::sqrt(double(kMaxSimTexels) / (w * h));
        w *= f;
        h *= f;
    }
    out.simW = static_cast<uint32_t>(std::clamp(std::lround(w), 8L, long(kMaxSimSide)));
    out.simH = static_cast<uint32_t>(std::clamp(std::lround(h), 8L, long(kMaxSimSide)));
    return PF_Err_NONE;
}

PF_Err ReadStepParams(PF_InData* in_data, A_long time, A_long step, A_u_long scale, const SimParams& base,
                      SimParams& p)
{
    PF_Err err = PF_Err_NONE;
    Checkouts c(in_data, time, step, scale);
    p = base;
    p.stepsPerFrame = static_cast<uint32_t>(std::round(c.Float(P_STEPS, err)));
    p.boundary = c.Popup(P_BOUNDARY, err);
    p.decay = c.Float(P_DECAY, err) / 100.0f;
    p.diffusionRadius = static_cast<uint32_t>(std::max(0.0f, std::round(c.Float(P_DIFF_RADIUS, err))));
    p.diffusionStrength = c.Float(P_DIFF_STRENGTH, err) / 100.0f;
    p.trailClamp = c.Float(P_TRAIL_CLAMP, err);
    for (uint32_t s = 0; s < p.numSpecies; ++s) {
        SpeciesParams& sp = p.species[s];
        sp.sensorAngle = c.Float(SpeciesParam(s, SF_SENSOR_ANGLE), err) * kDeg;
        sp.sensorDist = c.Float(SpeciesParam(s, SF_SENSOR_DIST), err);
        sp.turnAngle = c.Float(SpeciesParam(s, SF_TURN_ANGLE), err) * kDeg;
        sp.stepSize = c.Float(SpeciesParam(s, SF_STEP_SIZE), err);
        sp.deposit = c.Float(SpeciesParam(s, SF_DEPOSIT), err);
        sp.jitter = c.Float(SpeciesParam(s, SF_JITTER), err) * kDeg;
        sp.otherWeight = c.Float(SpeciesParam(s, SF_OTHER), err) / 100.0f;
    }
    if (err) return err;
    Finalize(p);
    return PF_Err_NONE;
}

PF_Err ReadSimParams(PF_InData* in_data, A_long time, A_long step, A_u_long scale, const StructuralParams& st,
                     SimParams& p)
{
    PF_Err err = PF_Err_NONE;
    Checkouts c(in_data, time, step, scale);
    p = DefaultSimParams(st.simW, st.simH);

    p.numSpecies = st.numSpecies;
    p.agentCount = st.agentCount;
    p.seed = static_cast<uint32_t>(std::max(0.0f, std::round(c.Float(P_SEED, err))));
    p.stepsPerFrame = static_cast<uint32_t>(std::round(c.Float(P_STEPS, err)));
    p.preRoll = static_cast<uint32_t>(std::max(0.0f, std::round(c.Float(P_PREROLL, err))));
    p.boundary = c.Popup(P_BOUNDARY, err);

    p.decay = c.Float(P_DECAY, err) / 100.0f;
    p.diffusionRadius = static_cast<uint32_t>(std::max(0.0f, std::round(c.Float(P_DIFF_RADIUS, err))));
    p.diffusionStrength = c.Float(P_DIFF_STRENGTH, err) / 100.0f;
    p.trailClamp = c.Float(P_TRAIL_CLAMP, err);

    const double dsx = Ratio(in_data->downsample_x);
    const double dsy = Ratio(in_data->downsample_y);
    const double layerW = std::max<A_long>(in_data->width, 1);
    const double layerH = std::max<A_long>(in_data->height, 1);

    for (uint32_t s = 0; s < st.numSpecies; ++s) {
        SpeciesParams& sp = p.species[s];
        sp.share = c.Float(SpeciesParam(s, SF_SHARE), err) / 100.0f;
        c.Colour(SpeciesParam(s, SF_COLOUR), sp.colour, err);
        sp.sensorAngle = c.Float(SpeciesParam(s, SF_SENSOR_ANGLE), err) * kDeg;
        sp.sensorDist = c.Float(SpeciesParam(s, SF_SENSOR_DIST), err);
        sp.turnAngle = c.Float(SpeciesParam(s, SF_TURN_ANGLE), err) * kDeg;
        sp.stepSize = c.Float(SpeciesParam(s, SF_STEP_SIZE), err);
        sp.deposit = c.Float(SpeciesParam(s, SF_DEPOSIT), err);
        sp.jitter = c.Float(SpeciesParam(s, SF_JITTER), err) * kDeg;
        sp.otherWeight = c.Float(SpeciesParam(s, SF_OTHER), err) / 100.0f;
        sp.spawnPattern = c.Popup(SpeciesParam(s, SF_SPAWN), err);
        sp.headingMode = c.Popup(SpeciesParam(s, SF_HEADING), err);
        sp.spawnSize = c.Float(SpeciesParam(s, SF_SPAWN_SIZE), err) / 100.0f;
        if (const PF_ParamDef* d = c.Get(SpeciesParam(s, SF_SPAWN_CENTRE), err)) {
            // Point values are in downsampled layer pixels; convert to a fraction of the layer.
            const double x = d->u.td.x_value / 65536.0 / (dsx > 0 ? dsx : 1.0);
            const double y = d->u.td.y_value / 65536.0 / (dsy > 0 ? dsy : 1.0);
            sp.spawnCx = QuantiseFraction(x / layerW) * float(st.simW);
            sp.spawnCy = QuantiseFraction(y / layerH) * float(st.simH);
        }
    }

    p.palette = c.Popup(P_PALETTE, err);
    c.Colour(P_COL_A, p.colA, err);
    c.Colour(P_COL_B, p.colB, err);
    c.Colour(P_COL_C, p.colC, err);
    p.exposure = c.Float(P_EXPOSURE, err);
    p.gamma = c.Float(P_GAMMA, err);
    p.alphaFromIntensity = c.Check(P_ALPHA_FROM_INTENSITY, err) ? 1u : 0u;
    p.background = c.Popup(P_BACKGROUND, err);
    c.Colour(P_BG_COLOUR, p.bgColour, err);

    p.drawAgents = c.Check(P_DRAW_AGENTS, err) ? 1u : 0u;
    p.agentSize = static_cast<uint32_t>(std::round(c.Float(P_AGENT_SIZE, err)));
    p.agentOpacity = c.Float(P_AGENT_OPACITY, err) / 100.0f;
    if (err) return err;

    Finalize(p);
    return PF_Err_NONE;
}

PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                        const PF_UserChangedParamExtra* extra)
{
    (void)in_data;
    const PF_ParamIndex idx = extra->param_index;

    if (idx == P_PRESET) {
        const A_long choice = params[P_PRESET]->u.pd.value;
        size_t count = 0;
        const Preset* presets = GetPresets(count);
        if (choice <= kPresetCustom || size_t(choice - 2) >= count) return PF_Err_NONE;
        const Preset& pr = presets[choice - 2];

        auto setFloat = [&](int i, double v) {
            params[i]->u.fs_d.value = v;
            params[i]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        };
        auto setPopup = [&](int i, A_long v) {
            params[i]->u.pd.value = v;
            params[i]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        };
        setPopup(P_SPECIES, A_long(pr.species));
        setPopup(P_BOUNDARY, A_long(pr.boundary) + 1);
        setFloat(P_DECAY, pr.decayPct);
        setFloat(P_DIFF_RADIUS, pr.diffusionRadius);
        setFloat(P_DIFF_STRENGTH, pr.diffusionStrengthPct);
        setFloat(P_TRAIL_CLAMP, pr.trailClamp);
        for (uint32_t s = 0; s < kMaxSpecies; ++s) {
            const SpeciesPreset& sp = pr.sp[s];
            setFloat(SpeciesParam(s, SF_SENSOR_ANGLE), sp.sensorAngleDeg);
            setFloat(SpeciesParam(s, SF_SENSOR_DIST), sp.sensorDist);
            setFloat(SpeciesParam(s, SF_TURN_ANGLE), sp.turnAngleDeg);
            setFloat(SpeciesParam(s, SF_STEP_SIZE), sp.stepSize);
            setFloat(SpeciesParam(s, SF_DEPOSIT), sp.deposit);
            setFloat(SpeciesParam(s, SF_JITTER), sp.jitterDeg);
            setFloat(SpeciesParam(s, SF_OTHER), sp.otherPct);
        }
        out_data->out_flags |= PF_OutFlag_REFRESH_UI;
    } else if (IsPresetControlled(idx) && params[P_PRESET]->u.pd.value != kPresetCustom) {
        params[P_PRESET]->u.pd.value = kPresetCustom;
        params[P_PRESET]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        out_data->out_flags |= PF_OutFlag_REFRESH_UI;
    }
    return PF_Err_NONE;
}

PF_Err UpdateParamsUI(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    (void)out_data;
    if (!in_data->global_data) return PF_Err_NONE;
    const GlobalData* g = reinterpret_cast<const GlobalData*>(DH(in_data->global_data));
    if (!g || g->isPremiere || g->aegpId == 0) return PF_Err_NONE;

    const A_long speciesCount = params[P_SPECIES]->u.pd.value;
    const bool gradient = params[P_PALETTE]->u.pd.value == A_long(Palette::Gradient) + 1;
    const bool bgColour = params[P_BACKGROUND]->u.pd.value == A_long(Background::Colour) + 1;

    AEGP_SuiteHandler suites(in_data->pica_basicP);
    AEGP_EffectRefH effectH = nullptr;
    A_Err err = suites.PFInterfaceSuite1()->AEGP_GetNewEffectForEffect(g->aegpId, in_data->effect_ref, &effectH);
    if (err || !effectH) return PF_Err_NONE;

    auto setHidden = [&](int idx, bool hide) {
        AEGP_StreamRefH streamH = nullptr;
        if (suites.StreamSuite5()->AEGP_GetNewEffectStreamByIndex(g->aegpId, effectH, idx, &streamH) || !streamH)
            return;
        AEGP_DynStreamFlags flags = 0;
        if (!suites.DynamicStreamSuite4()->AEGP_GetDynamicStreamFlags(streamH, &flags)) {
            const bool hidden = (flags & AEGP_DynStreamFlag_HIDDEN) != 0;
            if (hidden != hide)
                suites.DynamicStreamSuite4()->AEGP_SetDynamicStreamFlag(streamH, AEGP_DynStreamFlag_HIDDEN, FALSE,
                                                                        hide ? TRUE : FALSE);
        }
        suites.StreamSuite5()->AEGP_DisposeStream(streamH);
    };

    for (uint32_t s = 1; s < kMaxSpecies; ++s) {
        const bool hide = A_long(s) >= speciesCount;
        for (int f = SF_TOPIC; f < SF_TOPIC_END; ++f) setHidden(SpeciesParam(s, SpeciesField(f)), hide);
    }
    setHidden(P_COL_A, !gradient);
    setHidden(P_COL_B, !gradient);
    setHidden(P_COL_C, !gradient);
    setHidden(P_BG_COLOUR, !bgColour);

    suites.EffectSuite5()->AEGP_DisposeEffect(effectH);
    return PF_Err_NONE;
}

} // namespace phys::ae
