#pragma once
// =============================================================
// settings.hpp
// =============================================================
#define TERRAIN_SETTINGS_HPP

#include <cstdint>

namespace TerrainSettings {
    // Continent
    static inline float CONTINENT_SCALE     = 0.0008f;
    static inline float HEIGHT_LACUNARITY   = 2.0f;
    static inline float HEIGHT_GAIN         = 0.5f;
    static inline float HEIGHT_SCALE        = 140.0f;
    static inline int   BASE_HEIGHT         = 64;

    // Mountains
    static inline float MOUNTAIN_SCALE      = 0.002f;
    static inline float MOUNTAIN_AMP        = 160.0f;

    // Detail
    static inline float HEIGHT_BASE_FREQ    = 0.004f;

    // Rivers
    static inline float RIVER_SCALE         = 0.003f;
    static inline float RIVER_WIDTH         = 0.8f;

    // Biome
    static inline float BIOME_SCALE         = 0.01f;

    // Water
    static inline int   WATER_LEVEL         = 62;

    // Caves
    static inline float CAVE_BASE_FREQ      = 0.04f;
    static inline int   CAVE_OCTAVES        = 3;
    static inline float CAVE_WORM_RADIUS    = 0.08f;
}

namespace WorldCfg {
    static constexpr int RENDER_DISTANCE       = 8;
    static constexpr int SIMULATION_DISTANCE   = 12;
    static constexpr int VERTICAL_SIM_DISTANCE = 4;
}