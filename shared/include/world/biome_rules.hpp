// biome_rules.hpp
#pragma once

#include "world/block.hpp"
#include "world/terrain_params.hpp"
#include "device/host_device.hpp"

#include <math.h>

// --- Biome lookup table on the device ---
// Temperature: 0=cold, 1=warm, 2=hot   (rows)
// Humidity:    0=dry,  1=mild, 2=wet   (cols)
#ifdef __CUDACC__
    static __device__ __constant__ Biome kBiomeTable[3][3] = {
        { Biome::SnowDesert, Biome::SnowyPlains, Biome::SnowyForest }, // cold
        { Biome::Rocky,      Biome::Plains,      Biome::Forest      }, // warm
        { Biome::Desert,     Biome::Savannah,    Biome::Jungle      }, // hot
    };
#else
    static const Biome kBiomeTable[3][3] = {
        { Biome::SnowDesert, Biome::SnowyPlains, Biome::SnowyForest }, // cold
        { Biome::Rocky,      Biome::Plains,      Biome::Forest      }, // warm
        { Biome::Desert,     Biome::Savannah,    Biome::Jungle      }, // hot
    };
#endif

// Returns biome from continuous temperature [0,1] and humidity [0,1]
HD FINLINE Biome classifyBiome(float temperature, float humidity) {
    int ti = (int)fminf(floorf(temperature * 3.0f), 2.0f); // 0,1,2
    int hi = (int)fminf(floorf(humidity    * 3.0f), 2.0f);
    return kBiomeTable[ti][hi];
}

// Per-biome surface/subsurface block selection.
// depthFromSurface: 0 = top surface block, 1 = first below
// Returns the BlockType to place
// Stone used as catch-all for deep rock.
HD FINLINE BlockType biomeBlock(Biome biome, int depthFromSurface, bool isSurface,
                     bool isUnderwater, int worldY, int waterLevel) {
    if (isUnderwater) {
        // River / lake bed: clay near bottom, gravel otherwise
        if (depthFromSurface <= 1) return BlockType::Clay;
        if (depthFromSurface <= 3) return BlockType::Gravel;
        return BlockType::Stone;
    }

    switch (biome) {
    case Biome::Desert:
    case Biome::SnowDesert:
        if (depthFromSurface == 0) return (biome == Biome::SnowDesert) ? BlockType::Snow : BlockType::Sand;
        if (depthFromSurface <= 3) return (biome == Biome::SnowDesert) ? BlockType::Permafrost : BlockType::Sand;
        if (depthFromSurface <= 6) return (biome == Biome::SnowDesert) ? BlockType::Stone : BlockType::Sandstone;
        return BlockType::Stone;

    case Biome::Savannah:
        if (depthFromSurface == 0) return BlockType::RedSand;
        if (depthFromSurface <= 4) return BlockType::RedSandstone;
        return BlockType::Stone;

    case Biome::Jungle:
        if (depthFromSurface == 0) return BlockType::Podzol;
        if (depthFromSurface <= 3) return BlockType::Dirt;
        return BlockType::Stone;

    case Biome::Rocky:
        if (depthFromSurface == 0) return BlockType::Gravel;
        return BlockType::Stone;

    case Biome::Plains:
    case Biome::Forest:
        if (depthFromSurface == 0) return BlockType::Grass;
        if (depthFromSurface <= 3) return BlockType::Dirt;
        return BlockType::Stone;

    case Biome::SnowyPlains:
    case Biome::SnowyForest:
        if (depthFromSurface == 0) return BlockType::Snow;
        if (depthFromSurface <= 2) return BlockType::Permafrost;
        if (depthFromSurface <= 4) return BlockType::Dirt;
        return BlockType::Stone;
    }
    return BlockType::Stone;
}