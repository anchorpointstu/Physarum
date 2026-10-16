#pragma once

// Plain-old-data parameter blocks shared between the host code and the shaders.
// SimParams is uploaded verbatim as constant buffer b1, so its layout must match
// src/shaders/Common.hlsli exactly: every group of four scalars is one 16-byte row.

#include <cstdint>

namespace phys {

constexpr uint32_t kMaxSpecies = 4;
constexpr uint32_t kMaxAgents = 10'000'000;
constexpr uint32_t kMaxSimSide = 8192;
constexpr uint64_t kMaxSimTexels = 16'777'216; // 4096 x 4096
constexpr float kDepositScale = 64.0f;         // fixed-point scale of the deposit atomics

enum class SpawnPattern : uint32_t { Random = 0, Centre, Ring, Box, Edge, Count };
enum class HeadingMode : uint32_t { Random = 0, Inward, Outward, Count };
enum class Boundary : uint32_t { Wrap = 0, Bounce, Respawn, Count };
enum class Palette : uint32_t { SpeciesColours = 0, Gradient, Fire, Ice, Neon, Mono, Bio, Count };
enum class Background : uint32_t { Transparent = 0, Black, Colour, Count };
enum class PixelFormat : uint32_t { U8 = 0, U16 = 1, F32 = 2 };

struct SpeciesParams {
    float sensorAngle;   // radians
    float sensorDist;    // simulation pixels
    float turnAngle;     // radians per step
    float stepSize;      // simulation pixels per step

    float deposit;       // trail added per agent per step (0..5)
    float jitter;        // radians, random turn added every step
    float otherWeight;   // -1 (avoid other species) .. 1 (follow them)
    float share;         // relative share of the agent count (structural)

    float colour[4];     // rgb 0..1, a unused

    uint32_t spawnPattern; // SpawnPattern (structural)
    uint32_t headingMode;  // HeadingMode (structural)
    uint32_t agentBegin;   // derived by Finalize()
    uint32_t agentEnd;     // derived by Finalize()

    float spawnCx;       // spawn centre, simulation pixels (structural)
    float spawnCy;
    float spawnSize;     // fraction of min(simW, simH) / 2 (structural)
    float pad0;
};
static_assert(sizeof(SpeciesParams) == 80);

struct SimParams {
    uint32_t numSpecies;      // 1..4 (structural)
    uint32_t seed;            // (structural)
    uint32_t agentCount;      // (structural)
    uint32_t stepsPerFrame;   // 1..10

    uint32_t preRoll;         // steps run before frame 0 (structural)
    uint32_t boundary;        // Boundary
    uint32_t diffusionRadius; // 0..2 texels
    uint32_t palette;         // Palette

    uint32_t simW;            // simulation resolution (structural)
    uint32_t simH;
    uint32_t background;      // Background
    uint32_t alphaFromIntensity;

    uint32_t drawAgents;
    uint32_t agentSize;       // 1..4 simulation pixels
    uint32_t pad0;
    uint32_t pad1;

    float decay;              // fraction lost per step, 0..1
    float diffusionStrength;  // 0..1
    float trailClamp;         // trail value that maps to full intensity, and the hard ceiling
    float exposure;

    float gamma;
    float agentOpacity;       // 0..1
    float pad2;
    float pad3;

    float colA[4];            // gradient stops
    float colB[4];
    float colC[4];
    float bgColour[4];

    SpeciesParams species[kMaxSpecies];
};
static_assert(sizeof(SimParams) == 480);
static_assert(sizeof(SimParams) % 16 == 0);

// Per-dispatch constants (b0) for every pass except present.
struct DispatchConstants {
    uint32_t count;      // items in a 1-D dispatch
    uint32_t groupsX;    // thread groups along x (1-D dispatches wrap into y)
    uint32_t stepIndex;  // global step counter, feeds the RNG
    uint32_t value;      // clear value / misc
};
static_assert(sizeof(DispatchConstants) == 16);

// Present constants (b0 of cs_present).
struct PresentConstants {
    uint32_t outW;
    uint32_t outH;
    uint32_t format;      // PixelFormat
    uint32_t taps;        // box-filter taps per axis when downscaling (1..4)

    float originX;        // output rect origin, output pixels relative to the layer
    float originY;
    float scaleX;         // simulation pixels per output pixel
    float scaleY;
};
static_assert(sizeof(PresentConstants) == 32);

// Describes the CPU destination of Present().
struct OutputDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rowBytes = 0;   // stride of the destination; may include padding
    PixelFormat format = PixelFormat::U8;
    float originX = 0.0f;    // output rect origin in output pixels (AE result_rect.left/top)
    float originY = 0.0f;
    float scaleX = 1.0f;     // simulation pixels per output pixel
    float scaleY = 1.0f;
};

inline uint32_t BytesPerPixel(PixelFormat f)
{
    return f == PixelFormat::U8 ? 4u : f == PixelFormat::U16 ? 8u : 16u;
}

// Defaults that match the After Effects parameter defaults.
SimParams DefaultSimParams(uint32_t simW, uint32_t simH);

// Clamps every field into its valid range and derives the per-species agent ranges.
void Finalize(SimParams& p);

} // namespace phys
