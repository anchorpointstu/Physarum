// Fills a buffer of 32-bit words with a constant (also used to zero float buffers).
#include "Common.hlsli"

cbuffer DispatchCB : register(b0)
{
    uint count;
    uint groupsX;
    uint stepIndex;
    uint value;
};

RWStructuredBuffer<uint> target : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID)
{
    uint id = LinearId(g, t, groupsX);
    if (id >= count) return;
    target[id] = value;
}
