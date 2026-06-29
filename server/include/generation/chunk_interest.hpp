#pragma once
// ==========================================================
// server/include/generation/chunk_interest.hpp
// Updated: split into compute (parallel-safe, read-only on
// playerStates) and commit (serial, writes playerStates +
// ChunkRegistry).  The compute output is a self-contained
// struct that the InterestCommit task applies.
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
    std::unordered_set<ChunkCoord, ChunkCoordHash> subscribedChunks;
};

// Result of one player's delta for ChunkRegistry (used by commit + disconnect)
struct InterestDelta {
    EntityId                playerId;
    std::vector<ChunkCoord> toSubscribe;    // newly entered render distance
    std::vector<ChunkCoord> toUnsubscribe;  // left render distance
};

// Result of one player's delta computation (parallel-safe output).
// This is the *read-only* result; it does NOT mutate playerStates.
// The InterestCommit task calls applyInterestOutput() to commit it.
struct InterestComputeOutput {
    EntityId  playerId = 0;
    ChunkCoord newLastChunk = { INT_MAX, INT_MAX, INT_MAX };
    std::vector<ChunkCoord>    toSubscribe;
    std::vector<ChunkCoord>    toUnsubscribe;
    std::unordered_set<ChunkCoord, ChunkCoordHash> newSubscribedChunks;
};

// Lightweight info for detecting which players moved
struct MovedPlayer {
    EntityId   playerId;
    glm::vec3  position;
    ChunkCoord currentChunk;
};

class ChunkInterestSystem {
public:
    // Detect which players crossed a chunk boundary this tick.
    // Safe to call from main thread; may lazily insert new entries
    // into playerStates (which is a non-const operation), so this
    // must be called BEFORE spawning parallel compute tasks.
    std::vector<MovedPlayer> detectMovedPlayers(Registry& ecs);

    // Compute interest delta for one player WITHOUT modifying
    // playerStates.  Thread-safe for different playerIds.
    // This is a const method — it only reads playerStates.
    InterestComputeOutput computeInterestReadonly(
        EntityId playerId, const glm::vec3& position) const;

    // Apply a previously computed output: update playerStates
    // and return the delta for the caller to feed into ChunkRegistry.
    // Must be called on the main thread only.
    InterestDelta applyInterestOutput(const InterestComputeOutput& output);

    // Remove player on disconnect (main thread only)
    InterestDelta removePlayer(EntityId id);

    void setRenderDistance(EntityId id, int renderDistance);

private:
    std::unordered_map<EntityId, PlayerInterestState> playerStates;

    static ChunkCoord toChunkCoord(const glm::vec3& pos);

    // Build the 3D cube of needed chunks and diff against subscribed set.
    // Pure computation; takes state by const& so it can be called from
    // computeInterestReadonly (which is const).
    static void computeDeltaReadonly(
        const PlayerInterestState& state,
        const ChunkCoord&          newCenter,
        std::vector<ChunkCoord>&   toSubscribe,
        std::vector<ChunkCoord>&   toUnsubscribe,
        std::unordered_set<ChunkCoord, ChunkCoordHash>& newSubscribed);
};