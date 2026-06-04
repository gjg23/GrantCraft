// chunk.cpp
#include <vector>
#include <stdio.h>

#include "world/chunk.hpp"

static const float TILE = 0.25f; // 1 tile height = 1/4 for 4 block types


/* ===== Chunk ===== */
Chunk::Chunk() {
    // autofill air
    blocks.assign(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE, BlockType::Air);
}

bool Chunk::isSolid(int localX, int localY, int localZ) {
    BlockType b = getBlockType(localX, localY, localZ);
    return isOpaque(b);
}

BlockType Chunk::getBlockType(int x, int y, int z) const {
    if (x<0||y<0||z<0||x>=CHUNK_SIZE||y>=CHUNK_SIZE||z>=CHUNK_SIZE) return BlockType::Air;  // outsize bounds
    return blocks[x + CHUNK_SIZE * (z + CHUNK_SIZE * y)];
}

void Chunk::setBlock(int x, int y, int z, BlockType b) {
    blocks[x + CHUNK_SIZE * (z + CHUNK_SIZE * y)] = b;
}