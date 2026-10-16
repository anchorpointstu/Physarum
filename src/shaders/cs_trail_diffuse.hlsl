// Box-blurs trailB into trailA, applies decay and the trail ceiling. A pure gather.
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

[numthreads(16, 16, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    if (dt.x >= simW || dt.y >= simH) return;
    int x = int(dt.x);
    int y = int(dt.y);
    int r = int(min(diffusionRadius, 2u));

    float4 centre = trailB[dt.y * simW + dt.x];
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            sum += trailB[TexelIndex(x + dx, y + dy)];
        }
    }
    float n = float((2 * r + 1) * (2 * r + 1));
    float4 v = lerp(centre, sum / n, saturate(diffusionStrength)) * (1.0 - saturate(decay));
    // Flush vanishing values to zero. A square blur kernel spreads trail faster along
    // diagonals than along the axes, and agents compare sensor values exactly, so
    // infinitesimal trail far ahead would otherwise steer them into a pixel-grid cross.
    v = v < TRAIL_EPSILON ? 0.0 : v;
    trailA[dt.y * simW + dt.x] = clamp(v, 0.0, max(trailClamp, 0.0));
}
