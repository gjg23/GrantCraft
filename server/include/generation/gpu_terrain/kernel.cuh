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

// extern __constant__ TerrainParams gTerrainParams;

void uploadTerrainParams(const TerrainParams& p);

// build a TerrainParams from the CPU-side namespace
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

// Single unified kernel
__global__
void terrainKernel(
    BlockType* blocks,
    int originX,
    int originY,
    int originZ);