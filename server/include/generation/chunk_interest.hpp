#pragma once
// ==========================================================
// server/include/generation/chunk_interest.hpp
// ==========================================================

#include "world/chunk.hpp"
#include "ecs/registry.hpp"
#include "settings/settings.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <climits>

using EntityId = uint32_t;

// Per-player chunk subscription state
struct PlayerInterestState {
    ChunkCoord lastChunk = { INT_MAX, INT_MAX, INT_MAX };
    int renderDistance = WorldCfg::SIMULATION_DISTANCE;
 
    // The set of chunk coords this player is currently subscribed to
    std::unordered_set<ChunkCoord, ChunkCoordHash> subscribedChunks;
};

// Result of one player's delta computation this tick
struct InterestDelta {
    EntityId                playerId;
    std::vector<ChunkCoord> toSubscribe;    // newly entered render distance
    std::vector<ChunkCoord> toUnsubscribe;  // left render distance
};

class ChunkInterestSystem {
public:
    // Returns one InterestDelta per player that actually moved
    std::vector<InterestDelta> computeDeltas(Registry& ecs);
 
    // Called by the server when a player disconnects
    InterestDelta removePlayer(EntityId id);
 
    void setRenderDistance(EntityId id, int renderDistance);
 
private:
    std::unordered_map<EntityId, PlayerInterestState> playerStates;
 
    static ChunkCoord toChunkCoord(const glm::vec3& pos);
 
    // Fills toSubscribe / toUnsubscribe by diffing old vs new set
    static void computeDelta(
        PlayerInterestState&     state,
        const ChunkCoord&        newCenter,
        std::vector<ChunkCoord>& toSubscribe,
        std::vector<ChunkCoord>& toUnsubscribe
    );
};