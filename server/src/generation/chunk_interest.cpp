// server/src/generation/chunk_interest.cpp
#include "generation/chunk_interest.hpp"
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
    const int r = state.renderDistance;

    // Build the set of chunks the player should have at new position
    std::unordered_set<ChunkCoord, ChunkCoordHash> needed;
    for (int dx = -r; dx <= r; ++dx)
    for (int dy = -r; dy <= r; ++dy)
    for (int dz = -r; dz <= r; ++dz) {
        ChunkCoord coord{ newCenter.x + dx, newCenter.y + dy, newCenter.z + dz };
        
        // Never queue below 0
        if (coord.y < 0) continue;
        
        needed.insert(coord);
    }

    // Chunks in
    for (auto& coord : needed)
        if (!state.requestedChunks.count(coord))
            toLoad.push_back(coord);

    // Chunks out
    for (auto& coord : state.requestedChunks)
        if (!needed.count(coord))
            toUnload.push_back(coord);
}

// Update loop called in server.cpp to enqueue needed chunks
void ChunkInterestSystem::update(Registry& ecs, ChunkSystem& chunkSystem) {
    // Build centers from all known player positions for priority scoring
    std::vector<ChunkCoord> centers;
    centers.reserve(playerStates.size());
    for (auto& [id, state] : playerStates) {
        if (state.lastChunk.x != INT_MAX)  // skip uninitialized
            centers.push_back(state.lastChunk);
    }
    
    // loop through all player entities
    for (auto& [id, pos] : ecs.allPositions()) {
        ChunkCoord center = toChunkCoord(pos.pos);

        // Lazily create state for new players
        auto& state = playerStates[id];

        // Only recompute delta if the player moved to a new chunk
        if (center == state.lastChunk) continue;
        
        // get chunks in and out
        std::vector<ChunkCoord> toLoad;
        std::vector<ChunkCoord> toUnload;
        computeDelta(id, state, center, toLoad, toUnload);

        // Submit new chunk interest
        for (auto& coord : toLoad) {
            state.requestedChunks.insert(coord);
            chunkSubscribers[coord].insert(id);
            chunkSystem.enqueueIfNeeded(coord, centers);
        }

        // Remove old interest
        for (auto& coord : toUnload) {
            state.requestedChunks.erase(coord);
            state.loadedChunks.erase(coord);

            auto it = chunkSubscribers.find(coord);

            if (it != chunkSubscribers.end())
            {
                it->second.erase(id);
                if (it->second.empty())
                    chunkSubscribers.erase(it);
            }
        }

        state.lastChunk = center;
    }
}

// Reverse lookup
const std::unordered_set<EntityId>*
ChunkInterestSystem::getSubscribers(const ChunkCoord& coord) const {
    auto it = chunkSubscribers.find(coord);

    if (it == chunkSubscribers.end())
        return nullptr;

    return &it->second;
}

// Mark chunk as sent to the player
void ChunkInterestSystem::markSent(EntityId id, const ChunkCoord& coord) {
    auto it = playerStates.find(id);
    if (it == playerStates.end())
        return;

    it->second.loadedChunks.insert(coord);
}

// Remove player from player states
void ChunkInterestSystem::removePlayer(EntityId id) {
    auto it = playerStates.find(id);
    if (it == playerStates.end()) return;

    for (const auto& coord : it->second.requestedChunks) {
        auto subIt = chunkSubscribers.find(coord);

        if (subIt == chunkSubscribers.end()) continue;

        subIt->second.erase(id);

        if (subIt->second.empty())
            chunkSubscribers.erase(subIt);
    }

    playerStates.erase(it);
}