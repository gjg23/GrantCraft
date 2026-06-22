// server/src/generation/chunk_interest.cpp
#include "generation/chunk_interest.hpp"
#include "generation/chunk_system.hpp"
#include <cmath>

// Convert world coord to chunk coord
ChunkCoord ChunkInterestSystem::toChunkCoord(const glm::vec3& pos) {
    return { (int)std::floor(pos.x / CHUNK_SIZE),
             (int)std::floor(pos.y / CHUNK_SIZE),
             (int)std::floor(pos.z / CHUNK_SIZE) };
}

// Build a 2D square of the needed chunks centered on the player
void ChunkInterestSystem::computeDelta(EntityId id,
                                       PlayerInterestState& state,
                                       const ChunkCoord& newCenter,
                                       std::vector<ChunkCoord>& toLoad,
                                       std::vector<ChunkCoord>& toUnload) {
    int r = state.renderDistance;

    // Build the set of chunks the player should have at new position
    std::unordered_set<ChunkCoord, ChunkCoordHash> needed;
    for (int dx = -r; dx <= r; ++dx)
        for (int dz = -r; dz <= r; ++dz)
            needed.insert({ newCenter.x + dx, 0, newCenter.z + dz });

    // Chunks in needed but not loaded → request generation + send
    for (auto& coord : needed)
        if (!state.loadedChunks.count(coord))
            toLoad.push_back(coord);

    // Chunks loaded but no longer needed → unload client-side
    for (auto& coord : state.loadedChunks)
        if (!needed.count(coord))
            toUnload.push_back(coord);
}

// Update loop called in server.cpp to enqueue needed chunks
void ChunkInterestSystem::update(Registry& ecs, ChunkSystem& chunkSystem) {
    // loop through all player entities
    for (auto& [id, pos] : ecs.allPositions()) {
        ChunkCoord center = toChunkCoord(pos.pos);

        // Lazily create state for new players
        auto& state = playerStates[id];

        // Only recompute delta if the player moved to a new chunk
        if (center == state.lastChunk) continue;
        
        state.lastChunk = center;

        // Get chunks needed to load and unload
        std::vector<ChunkCoord> toLoad, toUnload;
        computeDelta(id, state, center, toLoad, toUnload);

        // Submit needed chunks to the generation pipeline
        for (auto& coord : toLoad) chunkSystem.enqueueIfNeeded(coord);

        // Remove unloaded chunks from the player's loaded set
        // (client unload packet goes out in networkFlush)
        for (auto& coord : toUnload) state.loadedChunks.erase(coord);
    }
}

std::vector<EntityId> ChunkInterestSystem::getPeersNeedingChunk(const ChunkCoord& coord) {
    std::vector<EntityId> result;
    for (auto& [id, state] : playerStates) {
        auto& center = state.lastChunk;
        int dx = std::abs(coord.x - center.x);
        int dz = std::abs(coord.z - center.z);
        if (dx <= state.renderDistance && dz <= state.renderDistance && !state.loadedChunks.count(coord))
            result.push_back(id);
    }
    return result;
}

// Mark chunk as sent to the player
void ChunkInterestSystem::markSent(EntityId id, const ChunkCoord& coord) {
    playerStates[id].loadedChunks.insert(coord);
}

// Remove player from player states
void ChunkInterestSystem::removePlayer(EntityId id) {
    playerStates.erase(id);
}