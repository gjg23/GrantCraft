// server/src/generation/chunk_interest.cpp
#include "generation/chunk_interest.hpp"
#include <cmath>

// Convert world coord to chunk coord
ChunkCoord ChunkInterestSystem::toChunkCoord(const glm::vec3& pos) {
    return { (int)std::floor(pos.x / CHUNK_SIZE),
             (int)std::floor(pos.y / CHUNK_SIZE),
             (int)std::floor(pos.z / CHUNK_SIZE) };
}

// Build a 3D cube of the needed chunks centered on the player
void ChunkInterestSystem::computeDelta(
    PlayerInterestState&     state,
    const ChunkCoord&        newCenter,
    std::vector<ChunkCoord>& toSubscribe,
    std::vector<ChunkCoord>& toUnsubscribe)
{
    const int r = state.renderDistance;
 
    std::unordered_set<ChunkCoord, ChunkCoordHash> needed;
    needed.reserve((2*r+1) * (2*r+1) * (2*r+1));
 
    for (int dx = -r; dx <= r; ++dx)
    for (int dy = -r; dy <= r; ++dy)
    for (int dz = -r; dz <= r; ++dz) {
        ChunkCoord c{ newCenter.x + dx, newCenter.y + dy, newCenter.z + dz };
        if (c.y < 0) continue;      // never generate below bedrock
        needed.insert(c);
    }
 
    // Newly visible
    for (auto& coord : needed)
        if (!state.subscribedChunks.count(coord))
            toSubscribe.push_back(coord);
 
    // No longer visible
    for (auto& coord : state.subscribedChunks)
        if (!needed.count(coord))
            toUnsubscribe.push_back(coord);
 
    // Apply new set
    state.subscribedChunks = std::move(needed);
}

std::vector<InterestDelta> ChunkInterestSystem::computeDeltas(Registry& ecs) {
    std::vector<InterestDelta> results;
 
    for (auto& [id, pos] : ecs.allPositions()) {
        ChunkCoord center = toChunkCoord(pos.pos);
 
        // Lazily create state for new players
        auto& state = playerStates[id];
 
        // Skip players that haven't crossed a chunk boundary
        if (center == state.lastChunk) continue;
 
        InterestDelta delta;
        delta.playerId = id;
 
        computeDelta(state, center, delta.toSubscribe, delta.toUnsubscribe);
 
        state.lastChunk = center;
 
        if (!delta.toSubscribe.empty() || !delta.toUnsubscribe.empty())
            results.push_back(std::move(delta));
    }
 
    return results;
}

InterestDelta ChunkInterestSystem::removePlayer(EntityId id) {
    InterestDelta delta;
    delta.playerId = id;
 
    auto it = playerStates.find(id);
    if (it != playerStates.end()) {
        // Return everything they were subscribed to as unsubscribes
        for (auto& coord : it->second.subscribedChunks)
            delta.toUnsubscribe.push_back(coord);
        playerStates.erase(it);
    }

    return delta;
}

void ChunkInterestSystem::setRenderDistance(EntityId id, int renderDistance) {
    playerStates[id].renderDistance = renderDistance;
}