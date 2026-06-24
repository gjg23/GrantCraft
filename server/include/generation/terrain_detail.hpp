#pragma once
// generation/terrain_detail.hpp

#include "generation/device_shared/biome_rules.hpp"
#include "world/chunk.hpp"
#include "settings/settings.hpp"

// Cave lattice grid dimensions — derived from chunk size, not tunable
static constexpr int CAVE_STEP_XZ = 4;
static constexpr int CAVE_STEP_Y  = 8;
static constexpr int CAVE_LX      = (CHUNK_SIZE / CAVE_STEP_XZ) + 1;
static constexpr int CAVE_LY      = (CHUNK_SIZE / CAVE_STEP_Y)  + 1;
static constexpr int CAVE_LZ      = (CHUNK_SIZE / CAVE_STEP_XZ) + 1;

struct ColumnData {
    float surfaceY;
    float temperature;
    float humidity;
    Biome biome;
};

struct CaveLattice {
    float n1[CAVE_LY][CAVE_LZ][CAVE_LX];
    float n2[CAVE_LY][CAVE_LZ][CAVE_LX];
};

void buildColumnCache(ColumnData* cache, int originX, int originZ);
void buildCaveLattice(CaveLattice& lat, int originX, int originY, int originZ);