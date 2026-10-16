// Shared declarations for all Physarum compute shaders.
// SimParams must match src/sim/SimParams.h byte for byte.

#ifndef PHYSARUM_COMMON_HLSLI
#define PHYSARUM_COMMON_HLSLI

#define PI 3.14159265358979
#define TWO_PI 6.28318530717959
#define DEPOSIT_SCALE 64.0
#define INV_DEPOSIT_SCALE (1.0 / 64.0)
#define TRAIL_EPSILON 0.001

#define BOUNDARY_WRAP 0
#define BOUNDARY_BOUNCE 1
#define BOUNDARY_RESPAWN 2

struct SpeciesParams {
    float sensorAngle;
    float sensorDist;
    float turnAngle;
    float stepSize;

    float deposit;
    float jitter;
    float otherWeight;
    float share;

    float4 colour;

    uint spawnPattern;
    uint headingMode;
    uint agentBegin;
    uint agentEnd;

    float spawnCx;
    float spawnCy;
    float spawnSize;
    float pad0;
};

cbuffer SimParamsCB : register(b1)
{
    uint numSpecies;
    uint seed;
    uint agentCount;
    uint stepsPerFrame;

    uint preRoll;
    uint boundary;
    uint diffusionRadius;
    uint palette;

    uint simW;
    uint simH;
    uint background;
    uint alphaFromIntensity;

    uint drawAgents;
    uint agentSize;
    uint simPad0;
    uint simPad1;

    float decay;
    float diffusionStrength;
    float trailClamp;
    float exposure;

    float gammaValue;
    float agentOpacity;
    float simPad2;
    float simPad3;

    float4 colA;
    float4 colB;
    float4 colC;
    float4 bgColour;

    SpeciesParams species[4];
};

struct Agent {
    float2 pos;     // simulation pixels
    float heading;  // radians, kept in [0, 2pi)
    uint speciesId;
};

// ---------------------------------------------------------------- RNG
// Stateless: every random number is a hash of (agent, step, seed, salt), so the
// result never depends on thread scheduling.
uint Pcg(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint Hash4(uint a, uint b, uint c, uint d)
{
    return Pcg(a ^ Pcg(b ^ Pcg(c ^ Pcg(d))));
}

// Uniform float in [0, 1) from the top 24 bits.
float U01(uint h)
{
    return float(h >> 8) * (1.0 / 16777216.0);
}

// ---------------------------------------------------------------- 1-D dispatch helper
// 1-D work is dispatched as groupsX x N groups of 256 threads to stay under the
// 65535 groups-per-dimension limit.
uint LinearId(uint3 groupId, uint3 threadId, uint groupsX)
{
    return (groupId.y * groupsX + groupId.x) * 256u + threadId.x;
}

// ---------------------------------------------------------------- trail addressing
int WrapInt(int v, int size)
{
    // Floor division in float is exact for the coordinate range used here (< 2^24).
    int m = v - int(floor(float(v) / float(size))) * size;
    return clamp(m, 0, size - 1);
}

// Texel index for an integer coordinate, wrapping or clamping per boundary mode.
uint TexelIndex(int x, int y)
{
    if (boundary == BOUNDARY_WRAP) {
        x = WrapInt(x, int(simW));
        y = WrapInt(y, int(simH));
    } else {
        x = clamp(x, 0, int(simW) - 1);
        y = clamp(y, 0, int(simH) - 1);
    }
    return uint(y) * simW + uint(x);
}

float2 Dir(float a)
{
    float s, c;
    sincos(a, s, c);
    return float2(c, s);
}

float WrapAngle(float a)
{
    return a - floor(a * (1.0 / TWO_PI)) * TWO_PI;
}

// ---------------------------------------------------------------- spawning
// Places agent `id` of species `s` using hash salt `salt`. Used at reset and by
// the Respawn boundary mode.
Agent SpawnAgent(uint id, uint s, uint salt)
{
    SpeciesParams sp = species[s];
    float u = U01(Hash4(id, salt, seed, 0x51u));
    float v = U01(Hash4(id, salt, seed, 0xA7u));
    float w = U01(Hash4(id, salt, seed, 0x3Cu));

    float W = float(simW);
    float H = float(simH);
    float2 c = float2(sp.spawnCx, sp.spawnCy);
    float R = max(sp.spawnSize * 0.5 * min(W, H), 0.0);
    float2 p;

    if (sp.spawnPattern == 1u) {          // Centre: uniform disc
        float r = R * sqrt(u);
        p = c + Dir(TWO_PI * v) * r;
    } else if (sp.spawnPattern == 2u) {   // Ring: thin annulus
        float r = R * (0.97 + 0.06 * u);
        p = c + Dir(TWO_PI * v) * r;
    } else if (sp.spawnPattern == 3u) {   // Box
        p = c + R * float2(2.0 * u - 1.0, 2.0 * v - 1.0);
    } else if (sp.spawnPattern == 4u) {   // Edge of the frame, 1 px inset
        float perimeter = 2.0 * (W + H);
        float t = u * perimeter;
        if (t < W)               p = float2(t, 1.0);
        else if (t < W + H)      p = float2(W - 1.0, t - W);
        else if (t < 2.0 * W + H) p = float2(2.0 * W + H - t, H - 1.0);
        else                     p = float2(1.0, perimeter - t);
    } else {                              // Random over the whole frame
        p = float2(u * W, v * H);
    }

    p = clamp(p, float2(0.0, 0.0), float2(W - 0.001, H - 0.001));

    float heading;
    float2 toCentre = c - p;
    bool atCentre = dot(toCentre, toCentre) < 1e-6;
    if (sp.headingMode == 1u && !atCentre)       heading = atan2(toCentre.y, toCentre.x);
    else if (sp.headingMode == 2u && !atCentre)  heading = atan2(-toCentre.y, -toCentre.x);
    else                                          heading = TWO_PI * w;

    Agent a;
    a.pos = p;
    a.heading = WrapAngle(heading);
    a.speciesId = s;
    return a;
}

uint SpeciesOf(uint id)
{
    uint s = 0;
    [unroll] for (uint i = 1; i < 4; ++i) {
        if (i < numSpecies && id >= species[i].agentBegin) s = i;
    }
    return s;
}

#endif
