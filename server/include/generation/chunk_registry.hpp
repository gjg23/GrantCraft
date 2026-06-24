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
};

// debugging stats
struct RegistryStats {
    uint32_t requested              = 0;
    uint32_t generating             = 0;
    uint32_t worldReady             = 0;
    uint32_t sent                   = 0;
    uint32_t totalPendingRecipients = 0;
};

// ---------------------------------------------------------------
// ChunkRegistry
// All methods must be called from the main server thread only
// ---------------------------------------------------------------
class ChunkRegistry {
public:
    // Is coord is already tracked?
    bool has(const ChunkCoord& coord) const;

    // Request a chunk for a subscriber in any state
    void request(const ChunkCoord& coord, EntityId subscriber);

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
    bool markSentTo(const ChunkCoord& coord, EntityId id);

    // Collect all WorldReady chunks that still have pending recipients
    struct SendWork {
        ChunkCoord             coord;
        std::vector<EntityId>  recipients;  // copy, safe to iterate
    };
    std::vector<SendWork> collectSendWork() const;

    // Collect all Requested chunks (not yet submitted to worker pool)
    std::vector<ChunkCoord> collectRequested() const;

    // Remove a chunk entirely
    void remove(const ChunkCoord& coord);

    // Debuging
    std::size_t size() const { return entries.size(); }
    RegistryStats getStats() const;

private:
    std::unordered_map<ChunkCoord, ChunkEntry, ChunkCoordHash> entries;
};