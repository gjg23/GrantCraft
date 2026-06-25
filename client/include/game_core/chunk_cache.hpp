#pragma once
// ------------------------------------------------------------------
// client/include/world/chunk_cache.hpp
//   Render     — has a GPU mesh, within renderDistance
//   Simulation — block data in RAM, within simulationDistance (> render)
//   Cold       — raw block bytes only, player has visited but moved away
// ------------------------------------------------------------------

#include "world/chunk.hpp"

#include <unordered_map>
#include <vector>
#include <optional>
#include <algorithm>
#include <cstdint>
#include <cstring>

// ---- Tier enum ----
enum class ChunkTier : uint8_t {
    Cold        = 0,   // block bytes only (cheapest)
    Simulation  = 1,   // block data vector in RAM, no GPU mesh
    Render      = 2,   // simulation data + GPU mesh exists
};

// ---- Per-entry ----
struct CachedChunk {
    ChunkTier tier = ChunkTier::Cold;
    bool rendererResident = false;
    std::vector<BlockType> blocks;
};

// ---- ChunkCache ----
class ChunkCache {
public:
    //  Configuration
    void setRenderDistance(int r) {
        m_renderDist = std::max(0, r);
        if (m_simDist < m_renderDist) {
            m_simDist = m_renderDist;
        }
        m_forceRetier = true;
    }

    void setSimulationDistance(int s) {
        m_simDist = std::max(m_renderDist, s);
        m_forceRetier = true;
    }

    void clear();

    // Returns the tier the chunk was placed into.
    ChunkTier receive(
        const ChunkCoord& coord, 
        std::vector<BlockType> blocks, 
        const ChunkCoord& playerChunk);

    // ---- Player movement ----
    struct TierDelta {
        std::vector<ChunkCoord> toEnterSimulation;
        std::vector<ChunkCoord> toLeaveSimulation;
        std::vector<ChunkCoord> toShow;
        std::vector<ChunkCoord> toHide;
        std::vector<ChunkCoord> toLoadRenderer;
        std::vector<ChunkCoord> toDropRenderer;
    };
    TierDelta onPlayerMoved(const ChunkCoord& newCenter);

    // ---- Accessors ----
    CachedChunk*       get(const ChunkCoord& c);
    const CachedChunk* get(const ChunkCoord& c) const;

    bool copyBlocks(const ChunkCoord& c, std::vector<BlockType>& out) const;
    bool has(const ChunkCoord& c) const { return m_chunks.count(c) > 0; }

    ChunkTier getTier(const ChunkCoord& c) const {
        auto it = m_chunks.find(c);
        return it != m_chunks.end() ? it->second.tier : ChunkTier::Cold;
    }

    void markRendererResident(const ChunkCoord& c);
    void markRendererReleased(const ChunkCoord& c);

    const std::unordered_map<ChunkCoord, CachedChunk, ChunkCoordHash>& all() const {
        return m_chunks;
    }

    std::size_t size() const { return m_chunks.size(); }

private:
    // defaults
    int m_renderDist = 32;   
    int m_simDist    = 48;

    ChunkCoord m_lastCenter{ INT_MAX, INT_MAX, INT_MAX };
    bool       m_forceRetier = true;

    std::unordered_map<ChunkCoord, CachedChunk, ChunkCoordHash> m_chunks;

    // ---- helpers -----------------------------------------------------
    static bool isValidCenter(const ChunkCoord& c) {
        return c.x != INT_MAX && c.y != INT_MAX && c.z != INT_MAX;
    }

    static int chebychevDist(const ChunkCoord& a, const ChunkCoord& b) {
        return std::max({ std::abs(a.x - b.x),
                          std::abs(a.y - b.y),
                          std::abs(a.z - b.z) });
    }

    ChunkTier targetTier(const ChunkCoord& coord, const ChunkCoord& center) const;
};