// Adds this step's deposits to the trail (trailB = trailA + accum) and clears the
// accumulator. Each thread touches only its own texel.
#include "Common.hlsli"

cbuffer DispatchCB : register(b0)
{
    uint count;
    uint groupsX;
    uint stepIndex;
    uint value;
};

RWStructuredBuffer<float4> trailA : register(u0);
RWStructuredBuffer<float4> trailB : register(u1);
RWStructuredBuffer<uint> trailAccum : register(u2);

[numthreads(16, 16, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    if (dt.x >= simW || dt.y >= simH) return;
    uint i = dt.y * simW + dt.x;
    uint b = i * 4u;
    float4 add = float4(trailAccum[b], trailAccum[b + 1u], trailAccum[b + 2u], trailAccum[b + 3u]) * INV_DEPOSIT_SCALE;
    trailAccum[b] = 0u;
    trailAccum[b + 1u] = 0u;
    trailAccum[b + 2u] = 0u;
    trailAccum[b + 3u] = 0u;
    trailB[i] = trailA[i] + add;
}
