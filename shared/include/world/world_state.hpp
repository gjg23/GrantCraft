#pragma once
// world_state.hpp
// structs to describe world to server + client
// rendering data is client only

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <memory>

#include "world/chunk.hpp"

struct WorldState {
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> chunks;

    Chunk* getChunk(const ChunkCoord& coord) {
        auto it = chunks.find(coord);
        return (it != chunks.end()) ? it->second.get() : nullptr;
    }

    Chunk& insertChunk(const ChunkCoord& coord) {
        auto& ptr = chunks[coord];
        if (!ptr) ptr = std::make_unique<Chunk>(coord);
        return *ptr;
    }
};