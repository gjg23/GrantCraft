#pragma once
// ==========================================================
// server/include/generation/chunk_interest.hpp
// ==========================================================
#include "world/chunk.hpp"
#include "ecs/registry.hpp"
#include "generation/chunk_system.hpp"
#include <unordered_set>
#include <vector>

// Per-player chunk subscription state.
// Tracks what a player currently has loaded and computes
// load/unload deltas as they move.
struct PlayerInterestState {
    ChunkCoord lastChunk = {INT_MAX, INT_MAX, INT_MAX};
    std::unordered_set<ChunkCoord, ChunkCoordHash> loadedChunks; // sent to this player
    int renderDistance = 8;
};

class ChunkInterestSystem {
public:
    // Called by server tick
    // computes deltas and submits to ChunkSystem
    void update(Registry& ecs, ChunkSystem& chunkSystem);

    // Called by networkFlush to check which ready chunks to send to whom
    // Returns peers that need a specific coord
    std::vector<EntityId> getPeersNeedingChunk(const ChunkCoord& coord);

    // Mark a chunk as delivered to a player
    void markSent(EntityId id, const ChunkCoord& coord);

    // Remove player state on disconnect
    void removePlayer(EntityId id);

private:
    std::unordered_map<EntityId, PlayerInterestState> playerStates;

    ChunkCoord toChunkCoord(const glm::vec3& pos);
    void computeDelta(EntityId id,
                      PlayerInterestState& state,
                      const ChunkCoord& newCenter,
                      std::vector<ChunkCoord>& toLoad,
                      std::vector<ChunkCoord>& toUnload);
};