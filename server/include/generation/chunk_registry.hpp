#pragma once
// =============================================================
// server/include/generation/chunk_registry.hpp
// The truth of what a chunk really is
// The heart and soul of all chunks if you will
// =============================================================

#include "world/chunk.hpp"

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <algorithm>

using EntityId = uint32_t;

enum class ChunkLifecycle : uint8_t {
    Requested,      // asked for by player
    Generating,     // submitted to ChunkWorkerPool
    WorldReady,     // in worldstate not sent yet
    Sent,           // set to current chunk subscribers
};

struct ChunkEntry {
    ChunkLifecycle state = ChunkLifecycle::Requested;

    // Entities that still need this chunk sent to them
    std::vector<EntityId> pendingRecipients;

    // Entities that have already received this chunk this session
    std::vector<EntityId> sentRecipients;

    // When the chunk was first requested
    uint64_t requestedAtUs = 0;
};

// debugging stats
struct RegistryStats {
    uint32_t requested              = 0;
    uint32_t generating             = 0;
    uint32_t worldReady             = 0;
    uint32_t sent                   = 0;
    uint32_t totalPendingRecipients = 0;
};

// For distance-sorted queries
struct RequestedChunkWithDist {
    ChunkCoord coord;
    int distSq;
};

// ---------------------------------------------------------------
// ChunkRegistry
// ---------------------------------------------------------------
class ChunkRegistry {
public:
    // Is coord is already tracked?
    bool has(const ChunkCoord& coord) const;

    // Request a chunk for a subscriber in any state
    void request(const ChunkCoord& coord, EntityId subscriber, uint64_t nowUs = 0);

    // Forget all interest from one entity
    void removeSubscriber(EntityId id, const ChunkCoord& coord);

    // Remove ALL interest from an entity across every tracked chunk
    void removeAllSubscriptions(EntityId id);

    // --- Advance state ---
    // Requested → Generating
    void markGenerating(const ChunkCoord& coord);
    // Generating → WorldReady
    void markWorldReady(const ChunkCoord& coord);
    // pending → sent
    bool markSentTo(const ChunkCoord& coord, EntityId id, uint64_t nowUs = 0);

    // Collect all WorldReady chunks that still have pending recipients
    struct SendWork {
        ChunkCoord              coord;
        std::vector<EntityId>   recipients;
        uint64_t                requestedAtUs = 0;
    };
    std::vector<SendWork> collectSendWork() const;

    // Collect all Requested chunks (not yet submitted to worker pool)
    std::vector<ChunkCoord> collectRequested() const;
    std::vector<RequestedChunkWithDist> collectRequestedByDistance(ChunkCoord center) const;

    // Remove a chunk entirely
    void remove(const ChunkCoord& coord);

    // Debuging
    std::size_t size() const { return entries.size(); }
    RegistryStats getStats() const;

    // Latency samples
    const std::vector<float>& getLatencySamplesMs() const {
        return m_latencySamples;
    }
    void clearLatencySamples() { m_latencySamples.clear(); }

private:
    std::unordered_map<ChunkCoord, ChunkEntry, ChunkCoordHash> entries;
    
    std::vector<float> m_latencySamples;    // delivery latency in ms
    static constexpr size_t MAX_LATENCY_SAMPLES = 4096;
};