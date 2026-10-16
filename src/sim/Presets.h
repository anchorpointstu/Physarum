#pragma once

// Built-in looks, in the units the After Effects controls use (degrees, percent).
// Shared by the plugin (Preset popup) and the CLI (--set preset=<name>).

#include "sim/SimParams.h"

#include <cstddef>

namespace phys {

struct SpeciesPreset {
    float sensorAngleDeg;
    float sensorDist;
    float turnAngleDeg;
    float stepSize;
    float deposit;
    float jitterDeg;
    float otherPct; // -100 .. 100
};

struct Preset {
    const char* name;
    uint32_t species;       // 1..4
    uint32_t boundary;      // Boundary
    float decayPct;
    uint32_t diffusionRadius;
    float diffusionStrengthPct;
    float trailClamp;
    SpeciesPreset sp[kMaxSpecies];
};

const Preset* GetPresets(size_t& count);
const Preset* FindPreset(const char* name); // case-insensitive, nullptr if unknown

// Writes a preset's values into SimParams (the controlled subset only).
void ApplyPreset(SimParams& p, const Preset& preset);

} // namespace phys
