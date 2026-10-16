#include "sim/Presets.h"

#include <cctype>

namespace phys {

namespace {

constexpr uint32_t W = static_cast<uint32_t>(Boundary::Wrap);

// sensorAngle, sensorDist, turnAngle, step, deposit, jitter, other
const Preset kPresets[] = {
    {"Nets", 1, W, 10.0f, 1, 100.0f, 10.0f,
     {{22.5f, 9.0f, 45.0f, 1.0f, 1.0f, 0.0f, -50.0f}, {22.5f, 9.0f, 45.0f, 1.0f, 1.0f, 0.0f, -50.0f},
      {22.5f, 9.0f, 45.0f, 1.0f, 1.0f, 0.0f, -50.0f}, {22.5f, 9.0f, 45.0f, 1.0f, 1.0f, 0.0f, -50.0f}}},
    {"Foam", 1, W, 5.0f, 1, 100.0f, 10.0f,
     {{45.0f, 25.0f, 30.0f, 1.5f, 1.0f, 5.0f, -50.0f}, {45.0f, 25.0f, 30.0f, 1.5f, 1.0f, 5.0f, -50.0f},
      {45.0f, 25.0f, 30.0f, 1.5f, 1.0f, 5.0f, -50.0f}, {45.0f, 25.0f, 30.0f, 1.5f, 1.0f, 5.0f, -50.0f}}},
    {"Worms", 1, W, 20.0f, 2, 100.0f, 10.0f,
     {{60.0f, 5.0f, 60.0f, 1.0f, 2.0f, 0.0f, -50.0f}, {60.0f, 5.0f, 60.0f, 1.0f, 2.0f, 0.0f, -50.0f},
      {60.0f, 5.0f, 60.0f, 1.0f, 2.0f, 0.0f, -50.0f}, {60.0f, 5.0f, 60.0f, 1.0f, 2.0f, 0.0f, -50.0f}}},
    {"Flow", 1, W, 3.0f, 1, 50.0f, 10.0f,
     {{15.0f, 20.0f, 8.0f, 2.0f, 0.5f, 2.0f, -50.0f}, {15.0f, 20.0f, 8.0f, 2.0f, 0.5f, 2.0f, -50.0f},
      {15.0f, 20.0f, 8.0f, 2.0f, 0.5f, 2.0f, -50.0f}, {15.0f, 20.0f, 8.0f, 2.0f, 0.5f, 2.0f, -50.0f}}},
    {"Blobs", 1, W, 15.0f, 1, 100.0f, 10.0f,
     {{60.0f, 40.0f, 90.0f, 3.0f, 2.0f, 40.0f, -50.0f}, {60.0f, 40.0f, 90.0f, 3.0f, 2.0f, 40.0f, -50.0f},
      {60.0f, 40.0f, 90.0f, 3.0f, 2.0f, 40.0f, -50.0f}, {60.0f, 40.0f, 90.0f, 3.0f, 2.0f, 40.0f, -50.0f}}},
    {"Coral", 2, W, 8.0f, 1, 100.0f, 10.0f,
     {{30.0f, 12.0f, 40.0f, 1.0f, 1.0f, 0.0f, -100.0f}, {30.0f, 12.0f, 40.0f, 1.0f, 1.0f, 0.0f, -100.0f},
      {30.0f, 12.0f, 40.0f, 1.0f, 1.0f, 0.0f, -100.0f}, {30.0f, 12.0f, 40.0f, 1.0f, 1.0f, 0.0f, -100.0f}}},
    {"Sponge", 1, W, 3.0f, 1, 100.0f, 5.0f,
     {{12.0f, 30.0f, 30.0f, 1.0f, 1.0f, 0.0f, -50.0f}, {12.0f, 30.0f, 30.0f, 1.0f, 1.0f, 0.0f, -50.0f},
      {12.0f, 30.0f, 30.0f, 1.0f, 1.0f, 0.0f, -50.0f}, {12.0f, 30.0f, 30.0f, 1.0f, 1.0f, 0.0f, -50.0f}}},
    {"Veins", 1, W, 2.0f, 1, 100.0f, 20.0f,
     {{10.0f, 40.0f, 12.0f, 1.5f, 0.5f, 0.0f, -50.0f}, {10.0f, 40.0f, 12.0f, 1.5f, 0.5f, 0.0f, -50.0f},
      {10.0f, 40.0f, 12.0f, 1.5f, 0.5f, 0.0f, -50.0f}, {10.0f, 40.0f, 12.0f, 1.5f, 0.5f, 0.0f, -50.0f}}},
};

bool EqualsNoCase(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b)
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}

} // namespace

const Preset* GetPresets(size_t& count)
{
    count = sizeof(kPresets) / sizeof(kPresets[0]);
    return kPresets;
}

const Preset* FindPreset(const char* name)
{
    for (const Preset& p : kPresets)
        if (EqualsNoCase(p.name, name)) return &p;
    return nullptr;
}

void ApplyPreset(SimParams& p, const Preset& preset)
{
    constexpr float kDeg = 3.14159265358979f / 180.0f;
    p.numSpecies = preset.species;
    p.boundary = preset.boundary;
    p.decay = preset.decayPct / 100.0f;
    p.diffusionRadius = preset.diffusionRadius;
    p.diffusionStrength = preset.diffusionStrengthPct / 100.0f;
    p.trailClamp = preset.trailClamp;
    for (uint32_t s = 0; s < kMaxSpecies; ++s) {
        const SpeciesPreset& sp = preset.sp[s];
        SpeciesParams& d = p.species[s];
        d.sensorAngle = sp.sensorAngleDeg * kDeg;
        d.sensorDist = sp.sensorDist;
        d.turnAngle = sp.turnAngleDeg * kDeg;
        d.stepSize = sp.stepSize;
        d.deposit = sp.deposit;
        d.jitter = sp.jitterDeg * kDeg;
        d.otherWeight = sp.otherPct / 100.0f;
    }
}

} // namespace phys
