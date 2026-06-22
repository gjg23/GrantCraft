#pragma once
// -------------------------------------------------
// server/include/generation/terrain_gen.hpp
// defines structures all entities share in game
// -------------------------------------------------

#include "world/chunk.hpp"
#include <memory>

std::unique_ptr<Chunk> generateTerrain(const ChunkCoord& coord);