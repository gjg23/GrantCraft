// client/src/world/chunk_cache.cpp

#include "game_core/chunk_cache.hpp"
#include <algorithm>

// ---- targetTier ----
ChunkTier ChunkCache::targetTier(const ChunkCoord& coord,
                                 const ChunkCoord& center) const {
    if (!isValidCenter(center)) return ChunkTier::Cold;

    const int d = chebychevDist(coord, center);

    if (d <= m_renderDist) return ChunkTier::Render;
    if (d <= m_simDist)    return ChunkTier::Simulation;
    return ChunkTier::Cold;
}

// ---- receive ----
ChunkTier ChunkCache::receive(const ChunkCoord& coord,
                               std::vector<BlockType> blocks,
                               const ChunkCoord& playerChunk) {
    ChunkTier desired = targetTier(coord, playerChunk);

    auto& entry   = m_chunks[coord];

    entry.blocks  = std::move(blocks);
    entry.tier    = desired;
    
    return desired;
}

// ---- onPlayerMoved ----
ChunkCache::TierDelta ChunkCache::onPlayerMoved(const ChunkCoord& newCenter) {
    TierDelta delta;

    if (newCenter == m_lastCenter && !m_forceRetier) {
        return delta;
    }

    m_lastCenter  = newCenter;
    m_forceRetier = false;

    for (auto& [coord, entry] : m_chunks) {
      const ChunkTier oldTier = entry.tier;
        const ChunkTier newTier = targetTier(coord, newCenter);

        if (oldTier == newTier) {
            continue;
        }

        // Crossed from Cold into Simulation/Render
        if (oldTier == ChunkTier::Cold && newTier != ChunkTier::Cold) {
            delta.toEnterSimulation.push_back(coord);
        }

        // Crossed from Simulation/Render into Cold
        if (oldTier != ChunkTier::Cold && newTier == ChunkTier::Cold) {
            delta.toLeaveSimulation.push_back(coord);

            if (entry.rendererResident) {
                delta.toDropRenderer.push_back(coord);
            }
        }

        // Entered render distance
        if (oldTier != ChunkTier::Render && newTier == ChunkTier::Render) {
            if (!entry.rendererResident) {
                delta.toLoadRenderer.push_back(coord);
            }

            delta.toShow.push_back(coord);
        }

        // Left render distance but still within simulation distance
        if (oldTier == ChunkTier::Render && newTier == ChunkTier::Simulation) {
            delta.toHide.push_back(coord);
        }

        entry.tier = newTier;
    }

    return delta;
}

// ---- accessors --------------------------------------------------------
CachedChunk* ChunkCache::get(const ChunkCoord& c) {
    auto it = m_chunks.find(c);
    return it != m_chunks.end() ? &it->second : nullptr;
}

const CachedChunk* ChunkCache::get(const ChunkCoord& c) const {
    auto it = m_chunks.find(c);
    return it != m_chunks.end() ? &it->second : nullptr;
}

bool ChunkCache::copyBlocks(const ChunkCoord& c, std::vector<BlockType>& out) const {
    auto it = m_chunks.find(c);
    if (it == m_chunks.end()) return false;
    out = it->second.blocks;
    return true;
}

void ChunkCache::markRendererResident(const ChunkCoord& c) {
    auto it = m_chunks.find(c);
    if (it == m_chunks.end()) return;
    it->second.rendererResident = true;
}

void ChunkCache::markRendererReleased(const ChunkCoord& c) {
    auto it = m_chunks.find(c);
    if (it == m_chunks.end()) return;
    it->second.rendererResident = false;
}

void ChunkCache::clear() {
    m_chunks.clear();
    m_lastCenter  = { INT_MAX, INT_MAX, INT_MAX };
    m_forceRetier = true;
}