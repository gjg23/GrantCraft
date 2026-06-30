#include "generation/chunk_interest.hpp"
#include <cmath>

ChunkCoord ChunkInterestSystem::toChunkCoord(const glm::vec3& pos) {
    return { (int)std::floor(pos.x / CHUNK_SIZE),
             (int)std::floor(pos.y / CHUNK_SIZE),
             (int)std::floor(pos.z / CHUNK_SIZE) };
}

void ChunkInterestSystem::computeDeltaReadonly(
    const PlayerInterestState& state,
    const ChunkCoord&          newCenter,
    std::vector<ChunkCoord>&   toSubscribe,
    std::vector<ChunkCoord>&   toUnsubscribe,
    std::unordered_set<ChunkCoord, ChunkCoordHash>& newSubscribed)
{
    const int rH = state.renderDistance;
    const int rV = std::min(state.renderDistance, WorldCfg::VERTICAL_SIM_DISTANCE);

    newSubscribed.reserve((2*rH+1) * (2*rH+1) * (2*rV+1));
    for (int dx = -rH; dx <= rH; ++dx)
        for (int dy = -rV; dy <= rV; ++dy)
            for (int dz = -rH; dz <= rH; ++dz) {
                ChunkCoord c{ newCenter.x + dx, newCenter.y + dy, newCenter.z + dz };
                if (c.y < 0) continue;
                newSubscribed.insert(c);
            }

    // Newly visible
    for (const auto& coord : newSubscribed)
        if (!state.subscribedChunks.count(coord))
            toSubscribe.push_back(coord);

    // No longer visible
    for (const auto& coord : state.subscribedChunks)
        if (!newSubscribed.count(coord))
            toUnsubscribe.push_back(coord);
}

std::vector<MovedPlayer> ChunkInterestSystem::detectMovedPlayers(Registry& ecs) {
    std::vector<MovedPlayer> moved;
    for (auto& [id, pos] : ecs.allPositions()) {
        ChunkCoord center = toChunkCoord(pos.pos);
        // Lazily create state for new players (non-const — main thread only)
        auto& state = playerStates[id];

        if (center == state.lastChunk) continue;

        moved.push_back({id, pos.pos, center});
    }
    return moved;
}

InterestComputeOutput ChunkInterestSystem::computeInterestReadonly(
    EntityId playerId, const glm::vec3& position) const
{
    InterestComputeOutput output;
    output.playerId     = playerId;
    output.newLastChunk = toChunkCoord(position);

    auto it = playerStates.find(playerId);
    if (it == playerStates.end()) {
        // Brand-new player with no prior state: compute from empty set
        PlayerInterestState emptyState;
        emptyState.renderDistance = WorldCfg::SIMULATION_DISTANCE;
        computeDeltaReadonly(emptyState, output.newLastChunk,
                             output.toSubscribe, output.toUnsubscribe,
                             output.newSubscribedChunks);
    } else {
        // it->second is const PlayerInterestState& because this method
        // is const — computeDeltaReadonly takes const& so no qualifier drop.
        const PlayerInterestState& state = it->second;
        computeDeltaReadonly(state, output.newLastChunk,
                             output.toSubscribe, output.toUnsubscribe,
                             output.newSubscribedChunks);
    }
    return output;
}

InterestDelta ChunkInterestSystem::applyInterestOutput(
    const InterestComputeOutput& output)
{
    // Update player state (non-const — main thread only)
    auto& state = playerStates[output.playerId];
    state.lastChunk        = output.newLastChunk;
    state.subscribedChunks = output.newSubscribedChunks;

    // Return delta for the caller to feed into ChunkRegistry
    InterestDelta delta;
    delta.playerId      = output.playerId;
    delta.toSubscribe   = output.toSubscribe;
    delta.toUnsubscribe = output.toUnsubscribe;
    return delta;
}

InterestDelta ChunkInterestSystem::removePlayer(EntityId id) {
    InterestDelta delta;
    delta.playerId = id;
    auto it = playerStates.find(id);
    if (it != playerStates.end()) {
        for (const auto& coord : it->second.subscribedChunks)
            delta.toUnsubscribe.push_back(coord);
        playerStates.erase(it);
    }
    return delta;
}

void ChunkInterestSystem::setRenderDistance(EntityId id, int renderDistance) {
    playerStates[id].renderDistance = renderDistance;
}