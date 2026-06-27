#pragma once
// =============================================================
// settings.hpp
// =============================================================
#define TERRAIN_SETTINGS_HPP

#include <cstdint>

namespace TerrainSettings {
    // ---- Continents / sea level (large-scale land vs ocean) ----
    static inline float CONTINENT_SCALE   = 0.00035f;
    static inline float HEIGHT_SCALE      = 70.0f;    // continental rise
    static inline int   BASE_HEIGHT       = 64;
    static inline int   WATER_LEVEL       = 63;

    // ---- Musgrave multifractals (SIGGRAPH '89) ----
    static inline float MF_LACUNARITY     = 2.0f;     // frequency step per octave
    static inline int   MF_OCTAVES        = 8;        // detail levels (main perf lever)

    // Hybrid multifractal - heterogeneous land
    static inline float HYBRID_H          = 0.28f;    // fractal increment; LOW = rougher
    static inline float HYBRID_OFFSET     = 0.7f;
    static inline float HYBRID_SCALE      = 0.0009f;  // ~1100-block hill wavelength
    static inline float HYBRID_AMP        = 55.0f;    // hill height

    // Ridged multifractal - monumental mountain ranges
    static inline float RIDGE_H           = 0.9f;     // high = smoother ridge bodies
    static inline float RIDGE_OFFSET      = 1.0f;
    static inline float RIDGE_GAIN        = 2.0f;     // octave feedback; high = spikier
    static inline float RIDGE_SCALE       = 0.0016f;  // ~600-block ridge spacing
    static inline float RIDGE_AMP         = 170.0f;   // peak height

    // Where ranges occur (regional mask so mountains aren't everywhere)
    static inline float MOUNTAIN_MASK_SCALE = 0.0005f; // ~2000-block range regions
    static inline float MOUNTAIN_MASK_BIAS  = 0.55f;   // high = fewer/smaller ranges

    // ---- Detail + caves (unchanged use) ----
    static inline float HEIGHT_BASE_FREQ  = 0.004f;
    static inline float RIVER_SCALE       = 0.003f;
    static inline float RIVER_WIDTH       = 0.07f;
    static inline float BIOME_SCALE       = 0.0009f;
    static inline float CAVE_BASE_FREQ    = 0.04f;
    static inline int   CAVE_OCTAVES      = 3;
    static inline float CAVE_WORM_RADIUS  = 0.08f;

    // for cave carving
    static inline float CAVE_LACUNARITY  = 2.0f;
    static inline float CAVE_GAIN        = 0.5f;
}

namespace WorldCfg {
    static constexpr int RENDER_DISTANCE       = 8;
    static constexpr int SIMULATION_DISTANCE   = 12;
    static constexpr int VERTICAL_SIM_DISTANCE = 4;
}