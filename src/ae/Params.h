#pragma once

// Parameter table: indices (the order in the Effect Controls panel) and stable disk
// IDs. Never renumber disk IDs: saved projects reference them.

#include "ae/Physarum.h"
#include "sim/SimParams.h"

namespace phys::ae {

enum SpeciesField {
    SF_TOPIC = 0,
    SF_SHARE,
    SF_COLOUR,
    SF_SENSOR_ANGLE,
    SF_SENSOR_DIST,
    SF_TURN_ANGLE,
    SF_STEP_SIZE,
    SF_DEPOSIT,
    SF_JITTER,
    SF_OTHER,
    SF_SPAWN,
    SF_SPAWN_CENTRE,
    SF_SPAWN_SIZE,
    SF_HEADING,
    SF_TOPIC_END,
    SPECIES_STRIDE
};

enum ParamIndex {
    P_INPUT = 0,
    P_PRESET,
    P_SPECIES,
    P_SEED,
    P_AGENTS,
    P_SIM_SCALE,
    P_STEPS,
    P_PREROLL,
    P_BOUNDARY,

    P_TRAIL_TOPIC,
    P_DECAY,
    P_DIFF_RADIUS,
    P_DIFF_STRENGTH,
    P_TRAIL_CLAMP,
    P_TRAIL_END,

    P_SPECIES_BASE,
    P_COLOUR_TOPIC = P_SPECIES_BASE + kMaxSpecies * SPECIES_STRIDE,
    P_PALETTE,
    P_COL_A,
    P_COL_B,
    P_COL_C,
    P_EXPOSURE,
    P_GAMMA,
    P_ALPHA_FROM_INTENSITY,
    P_BACKGROUND,
    P_BG_COLOUR,
    P_COLOUR_END,

    P_AGENTS_TOPIC,
    P_DRAW_AGENTS,
    P_AGENT_SIZE,
    P_AGENT_OPACITY,
    P_AGENTS_END,

    P_CACHE_TOPIC,
    P_SNAPSHOT_EVERY,
    P_CACHE_MB,
    P_CACHE_END,

    P_COUNT
};

constexpr int SpeciesParam(uint32_t s, SpeciesField f) { return P_SPECIES_BASE + int(s) * SPECIES_STRIDE + f; }

// Disk IDs.
enum DiskId {
    D_PRESET = 1,
    D_SPECIES,
    D_SEED,
    D_AGENTS,
    D_SIM_SCALE,
    D_STEPS,
    D_PREROLL,
    D_BOUNDARY,
    D_TRAIL_TOPIC = 20,
    D_DECAY,
    D_DIFF_RADIUS,
    D_DIFF_STRENGTH,
    D_TRAIL_CLAMP,
    D_TRAIL_END = 29,
    D_SPECIES_BASE = 100, // + 20 * s + field
    D_COLOUR_TOPIC = 200,
    D_PALETTE,
    D_COL_A,
    D_COL_B,
    D_COL_C,
    D_EXPOSURE,
    D_GAMMA,
    D_ALPHA_FROM_INTENSITY,
    D_BACKGROUND,
    D_BG_COLOUR,
    D_COLOUR_END = 219,
    D_AGENTS_TOPIC = 220,
    D_DRAW_AGENTS,
    D_AGENT_SIZE,
    D_AGENT_OPACITY,
    D_AGENTS_END = 229,
    D_CACHE_TOPIC = 240,
    D_SNAPSHOT_EVERY,
    D_CACHE_MB,
    D_CACHE_END = 249,
};

constexpr A_short kPresetCustom = 1; // popup values are 1-based

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data);

// Simulation geometry derived from the structural parameters and the layer size.
struct StructuralParams {
    uint32_t numSpecies = 1;
    uint32_t agentCount = 0;
    uint32_t simW = 0;
    uint32_t simH = 0;
    uint32_t snapshotEvery = 24;
    uint64_t cacheBytes = 0;
};

// Reads the structural (non-animatable) parameters at `time` via checkouts.
PF_Err ReadStructural(PF_InData* in_data, A_long time, A_long step, A_u_long scale, StructuralParams& out);

// Checks out every parameter at `time` and builds SimParams (used at reset and for present).
PF_Err ReadSimParams(PF_InData* in_data, A_long time, A_long step, A_u_long scale, const StructuralParams& sp,
                     SimParams& out);

// Copies `base` and checks out only the animatable dynamics a simulation step uses.
PF_Err ReadStepParams(PF_InData* in_data, A_long time, A_long step, A_u_long scale, const SimParams& base,
                      SimParams& out);

// UI handling: presets and showing/hiding controls.
PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                        const PF_UserChangedParamExtra* extra);
PF_Err UpdateParamsUI(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[]);

} // namespace phys::ae
