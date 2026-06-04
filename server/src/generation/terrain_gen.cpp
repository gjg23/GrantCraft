// server/generation/terrain_gen.cpp
#include "generation/terrain_gen.hpp"
#include "world/block.hpp"
#include <cmath>
#include <algorithm>

// Simple flat world heighmap
static int heightAt(int worldX, int worldZ) {
    float fx = worldX * 0.05f;
    float fz = worldZ * 0.05f;
    return 64 + (int)(12.f * std::sin(fx) * std::cos(fz));
}

// Generate single chunk
std::unique_ptr<Chunk> generateTerrain(const ChunkCoord& coord) {
    auto chunk = std::make_unique<Chunk>(coord);    // make chunk at coords
    chunk->blocks.resize(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE, BlockType::Air);

    int ox = coord.x * CHUNK_SIZE;
    int oz = coord.z * CHUNK_SIZE;

    for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            int surfaceY = heightAt(ox + lx, oz + lz);
            surfaceY = std::clamp(surfaceY, 0, CHUNK_SIZE - 1);

            for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
                BlockType b = BlockType::Air;
                if      (ly == surfaceY)     b = BlockType::Grass;
                else if (ly >= surfaceY - 3) b = BlockType::Dirt;
                else if (ly > 0)             b = BlockType::Stone;
                chunk->setBlock(lx, ly, lz, b);
            }
        }
    }
    return chunk;
}