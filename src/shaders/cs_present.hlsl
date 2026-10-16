// Resamples the trail to the output rectangle, maps it to colour, composites the
// agent overlay and background, and packs pixels in After Effects' memory layout
// (A, R, G, B channel order; 8-bit, 16-bit 0..32768, or float).
#include "Common.hlsli"

cbuffer PresentCB : register(b0)
{
    uint outW;
    uint outH;
    uint format;     // 0 = 8-bit, 1 = 16-bit, 2 = float
    uint taps;       // box-filter taps per axis when the output is smaller than the sim
    float originX;
    float originY;
    float scaleX;    // simulation pixels per output pixel
    float scaleY;
};

RWStructuredBuffer<float4> trailA : register(u0);
RWStructuredBuffer<uint> overlay : register(u1);
RWByteAddressBuffer outPixels : register(u2);

static const float3 kPalettes[5][8] = {
    { // Fire
        float3(0.00, 0.00, 0.00), float3(0.25, 0.02, 0.00), float3(0.55, 0.05, 0.00), float3(0.85, 0.20, 0.00),
        float3(1.00, 0.45, 0.00), float3(1.00, 0.70, 0.10), float3(1.00, 0.90, 0.45), float3(1.00, 1.00, 1.00) },
    { // Ice
        float3(0.00, 0.00, 0.00), float3(0.00, 0.05, 0.15), float3(0.00, 0.15, 0.35), float3(0.05, 0.30, 0.60),
        float3(0.15, 0.50, 0.80), float3(0.40, 0.70, 0.95), float3(0.70, 0.88, 1.00), float3(1.00, 1.00, 1.00) },
    { // Neon
        float3(0.00, 0.00, 0.00), float3(0.15, 0.00, 0.30), float3(0.45, 0.00, 0.60), float3(0.85, 0.00, 0.70),
        float3(1.00, 0.20, 0.55), float3(0.40, 0.60, 1.00), float3(0.20, 0.95, 1.00), float3(0.90, 1.00, 1.00) },
    { // Mono
        float3(0.000, 0.000, 0.000), float3(0.143, 0.143, 0.143), float3(0.286, 0.286, 0.286), float3(0.429, 0.429, 0.429),
        float3(0.571, 0.571, 0.571), float3(0.714, 0.714, 0.714), float3(0.857, 0.857, 0.857), float3(1.000, 1.000, 1.000) },
    { // Bio
        float3(0.00, 0.00, 0.00), float3(0.00, 0.08, 0.06), float3(0.00, 0.20, 0.15), float3(0.05, 0.38, 0.20),
        float3(0.20, 0.58, 0.15), float3(0.50, 0.78, 0.20), float3(0.80, 0.92, 0.45), float3(1.00, 1.00, 0.85) },
};

float3 PaletteLookup(uint index, float x)
{
    float f = saturate(x) * 7.0;
    uint i = min(uint(f), 6u);
    return lerp(kPalettes[index][i], kPalettes[index][i + 1u], f - float(i));
}

// Bilinear fetch of the trail at simulation coordinates (texel centres at +0.5).
float4 SampleTrail(float2 sp)
{
    float2 q = sp - 0.5;
    float2 f = frac(q);
    int2 i = int2(floor(q));
    float4 a = trailA[TexelIndex(i.x, i.y)];
    float4 b = trailA[TexelIndex(i.x + 1, i.y)];
    float4 c = trailA[TexelIndex(i.x, i.y + 1)];
    float4 d = trailA[TexelIndex(i.x + 1, i.y + 1)];
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float Tone(float t)
{
    float x = saturate(t / max(trailClamp, 1e-6) * exposure);
    return pow(x, 1.0 / max(gammaValue, 0.01));
}

[numthreads(8, 8, 1)]
void main(uint3 dt : SV_DispatchThreadID)
{
    if (dt.x >= outW || dt.y >= outH) return;

    // Area-average the trail over this output pixel's footprint.
    float2 base = (float2(originX, originY) + float2(dt.xy)) * float2(scaleX, scaleY);
    float2 footprint = float2(scaleX, scaleY);
    uint n = clamp(taps, 1u, 4u);
    float4 trail = float4(0.0, 0.0, 0.0, 0.0);
    for (uint ty = 0; ty < n; ++ty) {
        for (uint tx = 0; tx < n; ++tx) {
            float2 o = (float2(tx, ty) + 0.5) / float(n);
            trail += SampleTrail(base + o * footprint);
        }
    }
    trail /= float(n * n);

    // Colour as if composited over black (premultiplied by construction).
    float3 rgb = float3(0.0, 0.0, 0.0);
    if (palette == 0u) {
        [unroll] for (uint s = 0; s < 4; ++s) {
            if (s < numSpecies) rgb += Tone(trail[s]) * species[s].colour.rgb;
        }
    } else {
        float total = 0.0;
        [unroll] for (uint s = 0; s < 4; ++s) {
            if (s < numSpecies) total += trail[s];
        }
        float x = Tone(total);
        if (palette == 1u) {
            rgb = x < 0.5 ? lerp(colA.rgb, colB.rgb, x * 2.0) : lerp(colB.rgb, colC.rgb, x * 2.0 - 1.0);
        } else {
            rgb = PaletteLookup(min(palette - 2u, 4u), x);
        }
    }

    // Agent dots (nearest texel, so they stay crisp).
    if (drawAgents != 0u) {
        float2 c = base + 0.5 * footprint;
        int2 ip = clamp(int2(floor(c)), int2(0, 0), int2(int(simW) - 1, int(simH) - 1));
        uint ob = (uint(ip.y) * simW + uint(ip.x)) * 4u;
        [unroll] for (uint s = 0; s < 4; ++s) {
            if (s < numSpecies && overlay[ob + s] != 0u) {
                rgb = lerp(rgb, species[s].colour.rgb, saturate(agentOpacity));
            }
        }
    }

    rgb = saturate(rgb);
    float alpha = 1.0;
    if (background == 0u) {          // Transparent
        if (alphaFromIntensity != 0u) {
            alpha = max(rgb.r, max(rgb.g, rgb.b));
            rgb = alpha > 1e-6 ? saturate(rgb / alpha) : float3(0.0, 0.0, 0.0); // straight colour
        }
    } else if (background == 2u) {   // Colour: trail over the background colour
        float cover = max(rgb.r, max(rgb.g, rgb.b));
        rgb = saturate(rgb + bgColour.rgb * (1.0 - cover));
    }
    // Black: rgb as is, alpha 1.

    float4 argb = float4(alpha, rgb);
    uint pixel = dt.y * outW + dt.x;
    if (format == 0u) {
        uint4 q = uint4(argb * 255.0 + 0.5);
        outPixels.Store(pixel * 4u, q.x | (q.y << 8) | (q.z << 16) | (q.w << 24));
    } else if (format == 1u) {
        uint4 q = uint4(argb * 32768.0 + 0.5);
        outPixels.Store2(pixel * 8u, uint2(q.x | (q.y << 16), q.z | (q.w << 16)));
    } else {
        outPixels.Store4(pixel * 16u, asuint(argb));
    }
}
