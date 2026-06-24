#pragma once
// ==========================================
// chunk.h
// defines a chunk + helper functions
// ==========================================
#include "world/block.hpp"
#include "glm/glm.hpp"

#include <vector>
#include <cstdint>
#include <cstddef>
#include <unordered_map>


// 3D Cube of blocks
// No limit on world size
#define CHUNK_SIZE 16
#define CHUNK_VOLUME (CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE)

// Chunk world coordnates
struct ChunkCoord {
    int32_t x, y, z;

    bool operator==(const ChunkCoord& other) const {
        return x==other.x && y==other.y && z==other.z;
    }
};

// Chunk hash
struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        size_t h = 0;
        h ^= std::hash<int32_t>{}(c.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int32_t>{}(c.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int32_t>{}(c.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};


// ================================
// Chunk definition
// ================================
class Chunk {
public:
    // Lifecycle
    Chunk();
    explicit Chunk(const ChunkCoord& c) : coord(c) {}
    //~Chunk();

    // chunk storage
    std::vector<BlockType> blocks; // flat block array

    // World position
    ChunkCoord coord = {0, 0, 0};

    // ----- Helper utilities -----
    bool        isSolid(int localX, int localY, int localZ);
    BlockType   getBlockType(int x, int y, int z) const;
    void        setBlock(int x, int y, int z, BlockType b);
};

// Get chunk origin
inline glm::ivec3 chunkOrigin(const ChunkCoord& c) {
    return { c.x * CHUNK_SIZE, c.y * CHUNK_SIZE, c.z * CHUNK_SIZE };
}