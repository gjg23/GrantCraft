#pragma once
// =============================================================
// settings.hpp
// =============================================================
#define TERRAIN_SETTINGS_HPP

#include <cstdint>

namespace Game {
    static constexpr float PLAYER_WALK_SPEED    = 5.0f;
    static constexpr float PLAYER_SPRINT_SPEED  = 9.0f;
    static constexpr float PLAYER_JUMP_VELOCITY = 8.0f;
    static constexpr float GRAVITY              = -20.0f;
    static constexpr int   PLAYER_MAX_HEALTH    = 20;
}

namespace TerrainSettings {
    // Continent
    static inline float CONTINENT_SCALE     = 0.0008f;
    static inline float HEIGHT_LACUNARITY   = 2.0f;
    static inline float HEIGHT_GAIN         = 0.5f;
    static inline float HEIGHT_SCALE        = 80.0f;
    static inline int   BASE_HEIGHT         = 64;

    // Mountains
    static inline float MOUNTAIN_SCALE      = 0.002f;
    static inline float MOUNTAIN_AMP        = 120.0f;

    // Detail
    static inline float HEIGHT_BASE_FREQ    = 0.004f;

    // Rivers
    static inline float RIVER_SCALE         = 0.003f;
    static inline float RIVER_WIDTH         = 0.08f;

    // Biome
    static inline float BIOME_SCALE         = 0.001f;

    // Water
    static inline int   WATER_LEVEL         = 62;

    // Caves
    static inline float CAVE_BASE_FREQ      = 0.04f;
    static inline int   CAVE_OCTAVES        = 3;
    static inline float CAVE_WORM_RADIUS    = 0.08f;
}

namespace ServerCfg {
    static inline int   TICK_RATE           = 20;
    static inline float TICK_BUDGET_MS      = 45.0f;
    static inline int   RENDER_DIST         = 8;
    static inline int   MAX_PLAYERS         = 32;
}

namespace ClientCfg {
    static inline int   FOV                 = 90;
    static inline int   RENDER_DISTANCE     = 8;
    static inline bool  VSYNC               = true;
    static inline float MOUSE_SENSITIVITY   = 0.3f;
}