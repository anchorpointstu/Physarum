// Splats each agent as an agentSize x agentSize square into the overlay counters.
#include "Common.hlsli"

cbuffer DispatchCB : register(b0)
{
    uint count;
    uint groupsX;
    uint stepIndex;
    uint value;
};

RWStructuredBuffer<Agent> agents : register(u0);
RWStructuredBuffer<uint> overlay : register(u1); // 4 lanes (species) per texel

[numthreads(256, 1, 1)]
void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID)
{
    uint id = LinearId(g, t, groupsX);
    if (id >= count) return;
    Agent a = agents[id];
    uint s = min(a.speciesId, 3u);
    int size = int(clamp(agentSize, 1u, 4u));
    int2 base = int2(floor(a.pos)) - int((uint(size) - 1u) >> 1);
    for (int dy = 0; dy < size; ++dy) {
        for (int dx = 0; dx < size; ++dx) {
            int px = base.x + dx;
            int py = base.y + dy;
            if (px < 0 || py < 0 || px >= int(simW) || py >= int(simH)) continue;
            InterlockedAdd(overlay[(uint(py) * simW + uint(px)) * 4u + s], 1u);
        }
    }
}
