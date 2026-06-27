// kernel.cuh
#pragma once
#include "world/block.hpp"
#include "settings/settings.hpp"

struct TerrainParams {
    float continentScale;
    float heightScale;
    int   baseHeight;
    float heightBaseFreq;
    float riverScale;
    float riverWidth;
    float biomeScale;
    int   waterLevel;
    float caveBaseFreq;
    int   caveOctaves;
    float caveWormRadius;
    float mfLacunarity;
    int   mfOctaves;
    float hybridH, hybridOffset, hybridScale, hybridAmp;
    float ridgeH,  ridgeOffset,  ridgeGain, ridgeScale, ridgeAmp;
    float mountainMaskScale, mountainMaskBias;
    float caveLacunarity;
    float caveGain;
};

struct GpuColumn {
    float surfaceY;
    int   biome;
};

void uploadTerrainParams(const TerrainParams& p);

inline TerrainParams makeTerrainParams() {
    using namespace TerrainSettings;
    TerrainParams p{};
    p.continentScale    = CONTINENT_SCALE;
    p.heightScale       = HEIGHT_SCALE;
    p.baseHeight        = BASE_HEIGHT;
    p.heightBaseFreq    = HEIGHT_BASE_FREQ;
    p.riverScale        = RIVER_SCALE;
    p.riverWidth        = RIVER_WIDTH;
    p.biomeScale        = BIOME_SCALE;
    p.waterLevel        = WATER_LEVEL;
    p.caveBaseFreq      = CAVE_BASE_FREQ;
    p.caveOctaves       = CAVE_OCTAVES;
    p.caveWormRadius    = CAVE_WORM_RADIUS;
    p.mfLacunarity      = MF_LACUNARITY;
    p.mfOctaves         = MF_OCTAVES;
    p.hybridH           = HYBRID_H;
    p.hybridOffset      = HYBRID_OFFSET;
    p.hybridScale       = HYBRID_SCALE;
    p.hybridAmp         = HYBRID_AMP;
    p.ridgeH            = RIDGE_H;
    p.ridgeOffset       = RIDGE_OFFSET;
    p.ridgeGain         = RIDGE_GAIN;
    p.ridgeScale        = RIDGE_SCALE;
    p.ridgeAmp          = RIDGE_AMP;
    p.mountainMaskScale = MOUNTAIN_MASK_SCALE;
    p.mountainMaskBias  = MOUNTAIN_MASK_BIAS;
    p.caveLacunarity    = CAVE_LACUNARITY;
    p.caveGain          = CAVE_GAIN;
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