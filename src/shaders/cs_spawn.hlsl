// Places every agent according to its species' spawn pattern.
#include "Common.hlsli"

cbuffer DispatchCB : register(b0)
{
    uint count;
    uint groupsX;
    uint stepIndex;
    uint value;
};

RWStructuredBuffer<Agent> agents : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID)
{
    uint id = LinearId(g, t, groupsX);
    if (id >= count) return;
    agents[id] = SpawnAgent(id, SpeciesOf(id), 0u);
}
