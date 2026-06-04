#pragma once
// block.hpp
// just defines block ids

#include <cstdint>

enum class BlockType : uint8_t {
    Air             = 0,
    Stone           = 1,   // universal subsurface
    Dirt            = 2,   // universal sub-surface top layer
    Grass           = 3,   // temperate surface
    Sand            = 4,   // desert + river-bank
    Sandstone       = 5,   // desert deep layer
    RedSand         = 6,   // savannah surface
    RedSandstone    = 7,   // savannah deep layer
    Gravel          = 8,   // riverbed + mountain scree
    Snow            = 9,   // cold biome surface cap
    Ice             = 10,  // frozen water + cold wet lakes
    Water           = 11,  // rivers + lakes
    Clay            = 12,  // river + lake floor
    Wood            = 13,  // tree trunk
    Leaves          = 14,  // tree canopy
    Cactus          = 15,  // desert decoration
    SavannahGrass   = 16,
    Podzol          = 17,  // jungle floor
    Permafrost      = 18,  // cold-biome deep layer
    COUNT           = 19,  // total
};

// check if block isnt solid
inline bool isOpaque(BlockType b) {
    return b != BlockType::Air;
}