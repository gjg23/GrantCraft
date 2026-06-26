// kernel.cuh
#pragma once
#include "world/block.hpp"
#include "settings/settings.hpp"

struct TerrainParams {
    float continentScale;
    float heightLacunarity;
    float heightGain;
    float heightScale;
    int   baseHeight;
    float mountainScale;
    float mountainAmp;
    float heightBaseFreq;
    float riverScale;
    float riverWidth;
    float biomeScale;
    int   waterLevel;
    float caveBaseFreq;
    int   caveOctaves;
    float caveWormRadius;
};

struct GpuColumn {
    float surfaceY;
    int   biome;
};

void uploadTerrainParams(const TerrainParams& p);

inline TerrainParams makeTerrainParams() {
    using namespace TerrainSettings;
    TerrainParams p{};
    p.continentScale   = CONTINENT_SCALE;
    p.heightLacunarity = HEIGHT_LACUNARITY;
    p.heightGain       = HEIGHT_GAIN;
    p.heightScale      = HEIGHT_SCALE;
    p.baseHeight       = BASE_HEIGHT;
    p.mountainScale    = MOUNTAIN_SCALE;
    p.mountainAmp      = MOUNTAIN_AMP;
    p.heightBaseFreq   = HEIGHT_BASE_FREQ;
    p.riverScale       = RIVER_SCALE;
    p.riverWidth       = RIVER_WIDTH;
    p.biomeScale       = BIOME_SCALE;
    p.waterLevel       = WATER_LEVEL;
    p.caveBaseFreq     = CAVE_BASE_FREQ;
    p.caveOctaves      = CAVE_OCTAVES;
    p.caveWormRadius   = CAVE_WORM_RADIUS;
    return p;
}


__global__
void columnKernel(GpuColumn* cols, int originX, int originZ);

__global__
void caveLatticeKernel(float* n1, float* n2, int originX, int originY, int originZ);

__global__
void densityKernel(BlockType* blocks, const GpuColumn* cols,
                   const float* n1, const float* n2,
                   int originX, int originY, int originZ, int doCaves);