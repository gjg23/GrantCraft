// =============================================================
// server/generation/terrain_gen.cpp
// Orchestrates terrain generation for a single cubic chunk.
// Heavy lifting is in terrain_detail.cpp; noise/spline/biome
// logic lives in the shared math/ and world/ headers.
// =============================================================

#include "generation/terrain_gen.hpp"
#include "generation/terrain_detail.hpp"
#include "math/noise.hpp"
#include "world/biome_rules.hpp"

#include <cmath>
#include <algorithm>

using namespace Terrain;

Chunk generateTerrain(const ChunkCoord& coord) {
    Chunk chunk(coord);
    chunk.blocks.resize(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE, BlockType::Air);

    const int originX = coord.x * CHUNK_SIZE;
    const int originY = coord.y * CHUNK_SIZE;
    const int originZ = coord.z * CHUNK_SIZE;

    // Per-thread scratch — avoids heap allocation on every call
    static thread_local ColumnData colCache[CHUNK_SIZE * CHUNK_SIZE];
    static thread_local CaveLattice caveLat;

    buildColumnCache(colCache, originX, originZ);

    // Skip cave lattice for chunks entirely above the water+margin line
    const bool doCaves = (originY + CHUNK_SIZE) < (WATER_LEVEL + 4);
    if (doCaves) buildCaveLattice(caveLat, originX, originY, originZ);

    const float worldTopF = (float)(WORLD_HEIGHT_CHUNKS * CHUNK_SIZE);
    const float cf        = CAVE_BASE_FREQ;
    const float wormR     = CAVE_WORM_RADIUS;
    const int   wl        = WATER_LEVEL;

    for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
        const int wy = originY + ly;

        // Cave lattice Y-cell indices + fractional for this row
        const int   gy0 = ly / CAVE_STEP_Y;
        const int   gy1 = std::min(gy0 + 1, CAVE_LY - 1);
        const float tyF = (float)(ly % CAVE_STEP_Y) / (float)CAVE_STEP_Y;

        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            const int   gz0 = lz / CAVE_STEP_XZ;
            const int   gz1 = std::min(gz0 + 1, CAVE_LZ - 1);
            const float tzF = (float)(lz % CAVE_STEP_XZ) / (float)CAVE_STEP_XZ;

        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            const int   gx0 = lx / CAVE_STEP_XZ;
            const int   gx1 = std::min(gx0 + 1, CAVE_LX - 1);
            const float txF = (float)(lx % CAVE_STEP_XZ) / (float)CAVE_STEP_XZ;

            const ColumnData& col = colCache[lz * CHUNK_SIZE + lx];
            const float wy_f = (float)wy;
            const float wx   = (float)(originX + lx);
            const float wz   = (float)(originZ + lz);

            // ---- 3D surface perturbation ----
            // Fades out near the world ceiling to prevent floating terrain.
            float ceilFade = 1.0f - fmaxf(0.0f, (wy_f - worldTopF * 0.75f) / (worldTopF * 0.25f));
            ceilFade = fmaxf(0.0f, ceilFade);
            float pFreq   = HEIGHT_BASE_FREQ * 3.0f;
            float perturb = fbm3D(wx * pFreq, wy_f * pFreq, wz * pFreq,
                                  3, 1.8f, 0.5f) * ceilFade;
            perturb = (perturb - 0.5f) * 12.0f;

            // ---- Density → block type ----
            const float density = (col.surfaceY + perturb) - wy_f;

            BlockType block;
            if (density <= 0.0f) {
                block = (wy <= wl) ? BlockType::Water : BlockType::Air;
            } else {
                const int  depth        = (int)floorf(density);
                const bool isUnderwater = (wy <= wl);
                block = biomeBlock(col.biome, depth, (depth == 0), isUnderwater, wy, wl);
            }

            // ---- Cave carving ----
            if (doCaves && block != BlockType::Air && block != BlockType::Water && wy > 1) {
                if (wy < wl - 3 || wy > wl + 3) {

                    // Worley chambers — large open spaces deep underground
                    const float chamber    = worleyNoise3D(wx * cf, wy_f * cf, wz * cf);
                    const float depthBelow = (float)BASE_HEIGHT - wy_f;
                    const bool  isChambered = (depthBelow > 30.0f) && (chamber < 0.12f);

                    // Worm tunnels — trilinearly interpolated from sparse lattice
                    const float n1 = trilerp(
                        caveLat.n1[gy0][gz0][gx0], caveLat.n1[gy0][gz0][gx1],
                        caveLat.n1[gy0][gz1][gx0], caveLat.n1[gy0][gz1][gx1],
                        caveLat.n1[gy1][gz0][gx0], caveLat.n1[gy1][gz0][gx1],
                        caveLat.n1[gy1][gz1][gx0], caveLat.n1[gy1][gz1][gx1],
                        txF, tyF, tzF);
                    const float n2 = trilerp(
                        caveLat.n2[gy0][gz0][gx0], caveLat.n2[gy0][gz0][gx1],
                        caveLat.n2[gy0][gz1][gx0], caveLat.n2[gy0][gz1][gx1],
                        caveLat.n2[gy1][gz0][gx0], caveLat.n2[gy1][gz0][gx1],
                        caveLat.n2[gy1][gz1][gx0], caveLat.n2[gy1][gz1][gx1],
                        txF, tyF, tzF);

                    if (isChambered || (fabsf(n1 - 0.5f) < wormR && fabsf(n2 - 0.5f) < wormR))
                        block = BlockType::Air;
                }
            }

            chunk.setBlock(lx, ly, lz, block);
        }}
    }

    return chunk;
}