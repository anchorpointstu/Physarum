// One simulation step for every agent: sense, turn, move, handle the boundary,
// deposit. Reads only trailA (the previous step's trail) and writes only its own
// agent plus order-independent integer atomics, so the result is deterministic.
#include "Common.hlsli"

cbuffer DispatchCB : register(b0)
{
    uint count;
    uint groupsX;
    uint stepIndex;
    uint value;
};

RWStructuredBuffer<Agent> agents : register(u0);
RWStructuredBuffer<float4> trailA : register(u1);
RWStructuredBuffer<uint> trailAccum : register(u2); // 4 lanes (species) per texel

// Nearest-texel sample of the weighted trail. (Bilinear sensing was measured at 2.4x
// the cost of the whole step with no visible gain once tiny trail values are flushed.)
float Sense(float2 p, float4 weights)
{
    int2 ip = int2(floor(p));
    return dot(trailA[TexelIndex(ip.x, ip.y)], weights);
}

[numthreads(256, 1, 1)]
void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID)
{
    uint id = LinearId(g, t, groupsX);
    if (id >= count) return;

    Agent a = agents[id];
    uint s = min(a.speciesId, 3u);
    SpeciesParams sp = species[s];

    // Own species counts fully; the others by otherWeight (negative = repelled).
    float4 own = float4(s == 0u, s == 1u, s == 2u, s == 3u);
    float4 w = lerp(sp.otherWeight.xxxx, float4(1.0, 1.0, 1.0, 1.0), own);

    float h = a.heading;
    float fwd = Sense(a.pos + Dir(h) * sp.sensorDist, w);
    float lft = Sense(a.pos + Dir(h + sp.sensorAngle) * sp.sensorDist, w);
    float rgt = Sense(a.pos + Dir(h - sp.sensorAngle) * sp.sensorDist, w);

    uint r0 = Hash4(id, stepIndex, seed, 0x11u);
    uint r1 = Hash4(id, stepIndex, seed, 0x22u);

    if (fwd > lft && fwd > rgt) {
        // keep going straight
    } else if (fwd < lft && fwd < rgt) {
        h += (U01(r0) < 0.5 ? -1.0 : 1.0) * sp.turnAngle;
    } else if (lft > rgt) {
        h += sp.turnAngle;
    } else if (rgt > lft) {
        h -= sp.turnAngle;
    }
    h += (U01(r1) * 2.0 - 1.0) * sp.jitter;

    float2 p = a.pos + Dir(h) * sp.stepSize;
    float W = float(simW);
    float H = float(simH);

    if (boundary == BOUNDARY_WRAP) {
        p -= floor(p / float2(W, H)) * float2(W, H);
    } else if (boundary == BOUNDARY_BOUNCE) {
        if (p.x < 0.0)      { p.x = -p.x;          h = PI - h; }
        else if (p.x >= W)  { p.x = 2.0 * W - p.x; h = PI - h; }
        if (p.y < 0.0)      { p.y = -p.y;          h = -h; }
        else if (p.y >= H)  { p.y = 2.0 * H - p.y; h = -h; }
    } else {
        if (p.x < 0.0 || p.y < 0.0 || p.x >= W || p.y >= H) {
            Agent n = SpawnAgent(id, s, stepIndex);
            p = n.pos;
            h = n.heading;
        }
    }
    // Guard against float edge cases (e.g. p == W after a wrap).
    p = clamp(p, float2(0.0, 0.0), float2(W - 0.001, H - 0.001));

    a.pos = p;
    a.heading = WrapAngle(h);
    agents[id] = a;

    uint texel = uint(p.y) * simW + uint(p.x);
    uint amount = uint(max(sp.deposit, 0.0) * DEPOSIT_SCALE + 0.5);
    if (amount != 0u) {
        InterlockedAdd(trailAccum[texel * 4u + s], amount);
    }
}
