// =============================================================
// generation/terrain_detail.cpp
// Implements column cache and cave lattice construction.
// All heavy noise work lives here so terrain_gen.cpp stays thin.
// =============================================================

#include "generation/terrain_detail.hpp"
#include "math/noise.hpp"
#include "math/spline.hpp"

#include <algorithm>
#include <cmath>

using namespace Terrain;

// ------------------------------------------------------------------
// Column cache
// ------------------------------------------------------------------
void buildColumnCache(ColumnData* cache, int originX, int originZ) {
    for (int lz = 0; lz < CHUNK_SIZE; lz++)
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        float wx = (float)(originX + lx);
        float wz = (float)(originZ + lz);

        // ---- Continent shape ----
        float continent = fbm2D(wx * CONTINENT_SCALE, wz * CONTINENT_SCALE,
                                12, HEIGHT_LACUNARITY, HEIGHT_GAIN);
        float c = continent * 2.0f - 1.0f;
        c = c * (1.0f + fabsf(c) * 0.8f);                    // push extremes outward
        c = fmaxf(-1.0f, fminf(1.0f, c));
        float spline = heightSpline(c * 0.5f + 0.5f);

        // ---- Mountain modifier ----
        float mRaw     = fbm2D(wx * MOUNTAIN_SCALE, wz * MOUNTAIN_SCALE, 3, 2.0f, 0.5f);
        float mountain = fmaxf(0.0f, mRaw - 0.4f) * (1.0f / 0.6f);
        mountain = mountain * mountain;                        // sharpen peaks

        // ---- Fine detail ----
        float detailFreq = HEIGHT_BASE_FREQ * 4.0f;
        float detail = fbm2D(wx * detailFreq + 200.0f, wz * detailFreq + 200.0f,
                             4, 2.0f, 0.55f);
        detail = (detail - 0.5f) * 18.0f;

        // ---- River carving ----
        float riverRaw      = warpedNoise2D(wx * RIVER_SCALE, wz * RIVER_SCALE, 8.0f);
        float riverDist     = fabsf(riverRaw - 0.5f) * 2.0f;
        float riverStrength = fmaxf(0.0f, 1.0f - riverDist / RIVER_WIDTH);
        riverStrength       = riverStrength * riverStrength;
        float riverFloor    = (float)WATER_LEVEL - 2.0f;

        // ---- Final surface Y ----
        float surfaceY = (float)BASE_HEIGHT
                       + spline   * HEIGHT_SCALE
                       + mountain * MOUNTAIN_AMP
                       + detail;
        surfaceY = surfaceY + riverStrength * (riverFloor - surfaceY);

        // ---- Biome ----
        float temperature = fbm2D(wx * BIOME_SCALE + 1000.0f, wz * BIOME_SCALE + 1000.0f,
                                  3, 2.0f, 0.5f);
        float humidity    = fbm2D(wx * BIOME_SCALE + 5000.0f, wz * BIOME_SCALE + 5000.0f,
                                  3, 2.0f, 0.5f);
        temperature = fmaxf(0.0f, fminf(1.0f, temperature));
        humidity    = fmaxf(0.0f, fminf(1.0f, humidity));

        ColumnData& col = cache[lz * CHUNK_SIZE + lx];
        col.surfaceY    = surfaceY;
        col.temperature = temperature;
        col.humidity    = humidity;
        col.biome       = classifyBiome(temperature, humidity);
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
                                          CAVE_OCTAVES, HEIGHT_LACUNARITY, HEIGHT_GAIN, 6.0f);

        lat.n2[gy][gz][gx] = warpedFbm3D(wx * wormFreq + 31.7f,
                                          wy * wormFreq + 17.3f,
                                          wz * wormFreq + 53.1f,
                                          CAVE_OCTAVES, HEIGHT_LACUNARITY, HEIGHT_GAIN, 6.0f);
    }
}