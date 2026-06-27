#pragma once
// =============================================================
// generation/device_shared/column_field.hpp
// Musgrave terrain style
// =============================================================

#include "generation/device_shared/host_device.hpp"
#include "generation/device_shared/noise.hpp"
#include "generation/device_shared/spline.hpp"
#include "generation/device_shared/biome_rules.hpp"
#include <math.h>

// Every tunable the column math needs (mirrors TerrainParams fields).
struct ColumnParams {
    float continentScale;
    float heightScale;
    int   baseHeight;
    int   waterLevel;

    float mfLacunarity;
    int   mfOctaves;

    float hybridH, hybridOffset, hybridScale, hybridAmp;
    float ridgeH,  ridgeOffset,  ridgeGain, ridgeScale, ridgeAmp;
    float mountainMaskScale, mountainMaskBias;

    float riverScale, riverWidth;
    float biomeScale;
};

struct ColumnResult {
    float surfaceY;
    float temperature;
    float humidity;
    Biome biome;
};

// ---- helpers -------------------------------------------------
HD FINLINE float snoise2D(float x, float z) {
    return perlinNoise2D(x, z) * 2.0f - 1.0f;
}

HD FINLINE float smoothstepf(float a, float b, float x) {
    float t = (x - a) / (b - a);
    t = fmaxf(0.0f, fminf(1.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

// Hybrid multifractal
HD FINLINE float hybridMultifractal2D(float x, float z, float H,
                                      float lacunarity, int octaves, float offset) {
    const float pexp = powf(lacunarity, -H);   // amplitude factor per octave
    float ampl   = 1.0f;
    float result = (snoise2D(x, z) + offset) * ampl;
    float weight = result;
    x *= lacunarity; z *= lacunarity; ampl *= pexp;

    for (int i = 1; i < octaves; ++i) {
        if (weight > 1.0f) weight = 1.0f;
        float signal = (snoise2D(x, z) + offset) * ampl;
        result += weight * signal;
        weight *= signal;
        x *= lacunarity; z *= lacunarity; ampl *= pexp;
    }
    return result;
}

// Ridged multifractal
HD FINLINE float ridgedMultifractal2D(float x, float z, float H,
                                      float lacunarity, int octaves,
                                      float offset, float gain) {
    const float pexp = powf(lacunarity, -H);
    float ampl = 1.0f;

    float signal = snoise2D(x, z);
    signal = offset - fabsf(signal);
    signal *= signal;                 // sharpen ridge
    float result = signal;
    x *= lacunarity; z *= lacunarity; ampl *= pexp;

    for (int i = 1; i < octaves; ++i) {
        float weight = signal * gain;
        weight = fmaxf(0.0f, fminf(1.0f, weight));
        signal = snoise2D(x, z);
        signal = offset - fabsf(signal);
        signal *= signal;
        signal *= weight;             // feed previous octave forward
        result += signal * ampl;
        x *= lacunarity; z *= lacunarity; ampl *= pexp;
    }
    return result;
}

// ---- The column ---------------------------------------------
HD FINLINE ColumnResult computeColumn(float wx, float wz, const ColumnParams& P) {
    // 1. Continentalness: large-scale land vs ocean
    float continent = fbm2D(wx * P.continentScale, wz * P.continentScale, 5, 2.0f, 0.5f);
    float c = continent * 2.0f - 1.0f;
    c = c * (1.0f + fabsf(c) * 0.8f);
    c = fmaxf(-1.0f, fminf(1.0f, c));
    float cont = heightSpline(c * 0.5f + 0.5f);        // [-1,1], <0 = ocean
    float land = smoothstepf(-0.02f, 0.15f, cont);     // 0 sea .. 1 inland

    // 2. Hybrid multifractal — heterogeneous hills (additive on land only)
    float hm = hybridMultifractal2D(wx * P.hybridScale, wz * P.hybridScale,
                                    P.hybridH, P.mfLacunarity, P.mfOctaves, P.hybridOffset);
    hm = fmaxf(0.0f, hm - P.hybridOffset);             // plains -> ~0, never digs below base

    // 3. Ridged multifractal — monumental ranges, gated to mountain regions
    float ridge = ridgedMultifractal2D(wx * P.ridgeScale, wz * P.ridgeScale,
                                       P.ridgeH, P.mfLacunarity, P.mfOctaves,
                                       P.ridgeOffset, P.ridgeGain);
    float maskRaw = fbm2D(wx * P.mountainMaskScale + 900.0f,
                          wz * P.mountainMaskScale + 900.0f, 3, 2.0f, 0.5f);
    float mountainMask = smoothstepf(P.mountainMaskBias, P.mountainMaskBias + 0.2f, maskRaw);

    // 4. Assemble surface height
    float surfaceY = (float)P.baseHeight
                   + cont                 * P.heightScale
                   + land * hm            * P.hybridAmp
                   + land * mountainMask * ridge * P.ridgeAmp;

    // 5. Rivers — carve land, but not through high peaks
    float riverRaw      = warpedNoise2D(wx * P.riverScale, wz * P.riverScale, 8.0f);
    float riverDist     = fabsf(riverRaw - 0.5f) * 2.0f;
    float riverStrength = fmaxf(0.0f, 1.0f - riverDist / P.riverWidth);
    riverStrength       = riverStrength * riverStrength * land * (1.0f - mountainMask);
    float riverFloor    = (float)P.waterLevel - 2.0f;
    surfaceY = surfaceY + riverStrength * (riverFloor - surfaceY);

    // 6. Biome (with altitude cooling -> snow caps on tall peaks)
    float temperature = fbm2D(wx * P.biomeScale + 1000.0f, wz * P.biomeScale + 1000.0f, 3, 2.0f, 0.5f);
    float humidity    = fbm2D(wx * P.biomeScale + 5000.0f, wz * P.biomeScale + 5000.0f, 3, 2.0f, 0.5f);
    float altCool = fmaxf(0.0f, (surfaceY - (float)P.waterLevel) / 160.0f);
    temperature   = fmaxf(0.0f, fminf(1.0f, temperature - altCool * 0.6f));
    humidity      = fmaxf(0.0f, fminf(1.0f, humidity));

    ColumnResult r;
    r.surfaceY    = surfaceY;
    r.temperature = temperature;
    r.humidity    = humidity;
    r.biome       = classifyBiome(temperature, humidity);
    return r;
}