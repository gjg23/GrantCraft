// =============================================================
// generation/terrain_detail.cpp
// Implements column cache and cave lattice construction.
// All heavy noise work lives here so terrain_gen.cpp stays thin.
// =============================================================

#include "generation/terrain_detail.hpp"
#include "generation/device_shared/noise.hpp"
#include "generation/device_shared/spline.hpp"
#include "generation/device_shared/column_field.hpp"

#include <algorithm>
#include <cmath>

using namespace TerrainSettings;

// ------------------------------------------------------------------
// Column cache
// ------------------------------------------------------------------
void buildColumnCache(ColumnData* cache, int originX, int originZ) {
    ColumnParams P;
    P.continentScale        = CONTINENT_SCALE;
    P.heightScale           = HEIGHT_SCALE;
    P.baseHeight            = BASE_HEIGHT;
    P.waterLevel            = WATER_LEVEL;
    P.mfLacunarity          = MF_LACUNARITY;      
    P.mfOctaves             = MF_OCTAVES;
    P.hybridH               = HYBRID_H; 
    P.hybridOffset          = HYBRID_OFFSET; 
    P.hybridScale           = HYBRID_SCALE; 
    P.hybridAmp             = HYBRID_AMP;
    P.ridgeH                = RIDGE_H;  
    P.ridgeOffset           = RIDGE_OFFSET;
    P.ridgeGain             = RIDGE_GAIN;
    P.ridgeScale            = RIDGE_SCALE; 
    P.ridgeAmp              = RIDGE_AMP;
    P.mountainMaskScale     = MOUNTAIN_MASK_SCALE; 
    P.mountainMaskBias      = MOUNTAIN_MASK_BIAS;
    P.riverScale            = RIVER_SCALE; 
    P.riverWidth            = RIVER_WIDTH; 
    P.biomeScale            = BIOME_SCALE;

    for (int lz = 0; lz < CHUNK_SIZE; lz++)
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        float wx = (float)(originX + lx);
        float wz = (float)(originZ + lz);
        ColumnResult cr = computeColumn(wx, wz, P);

        ColumnData& col = cache[lz * CHUNK_SIZE + lx];
        col.surfaceY    = cr.surfaceY;
        col.temperature = cr.temperature;
        col.humidity    = cr.humidity;
        col.biome       = cr.biome;
    }
}

// ------------------------------------------------------------------
// Cave lattice
// ------------------------------------------------------------------
void buildCaveLattice(CaveLattice& lat, int originX, int originY, int originZ) {
    const float wormFreq = CAVE_BASE_FREQ * 0.2f;

    for (int gy = 0; gy < CAVE_LY; gy++)
    for (int gz = 0; gz < CAVE_LZ; gz++)
    for (int gx = 0; gx < CAVE_LX; gx++) {
        float wx = (float)(originX + gx * CAVE_STEP_XZ);
        float wy = (float)(originY + gy * CAVE_STEP_Y);
        float wz = (float)(originZ + gz * CAVE_STEP_XZ);

        // Two independent noise fields whose iso-surfaces intersect to form tunnels
        lat.n1[gy][gz][gx] = warpedFbm3D(wx * wormFreq,
                                          wy * wormFreq,
                                          wz * wormFreq,
                                          CAVE_OCTAVES, CAVE_LACUNARITY, CAVE_GAIN, 6.0f);

        lat.n2[gy][gz][gx] = warpedFbm3D(wx * wormFreq + 31.7f,
                                          wy * wormFreq + 17.3f,
                                          wz * wormFreq + 53.1f,
                                          CAVE_OCTAVES, CAVE_LACUNARITY, CAVE_GAIN, 6.0f);
    }
}