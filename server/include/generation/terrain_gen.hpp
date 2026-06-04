#pragma once
// -------------------------------------------------
// server/include/generation/terrain_gen.hpp
// defines structures all entities share in game
// -------------------------------------------------

#include "world/chunk.hpp"
#include <memory>

// Pure function: given a coord, fills and returns a Chunk.
// No locks, no shared state — safe to call from multiple threads.
std::unique_ptr<Chunk> generateTerrain(const ChunkCoord& coord);