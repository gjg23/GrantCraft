#pragma once
// ------------------------------------------------------------------
// client/include/render/chunk_renderer.hpp
// ------------------------------------------------------------------

#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <list>
#include <queue>
#include <chrono>
#include <algorithm>
#include <functional>
#include <cstdint>

#include <glad.h>
#include <glm/glm.hpp>

#include "world/chunk.hpp"
#include "render/chunk_mesher.hpp"
#include "render/frustum.hpp"
#include "settings/settings.hpp"

// ---------------------------------------------------------------------------
// GpuMesh — per-chunk VAO/VBO/EBO on the GPU (main thread only)
// ---------------------------------------------------------------------------
struct GpuMesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    int indexCount = 0;

    void upload(const std::vector<float>& verts, const std::vector<unsigned int>& idxs);
    void draw() const;
    void release();
};

// ---------------------------------------------------------------------------
// ChunkRenderer
// ---------------------------------------------------------------------------
class ChunkRenderer {
public:
    ChunkRenderer();
    ~ChunkRenderer();

    // Called when a new chunk packet arrives
    void onChunkReceived(const ChunkCoord& coord, std::vector<BlockType> blocks);
    void setChunkVisible(const ChunkCoord& coord, bool visible);
    void onChunkRemoved(const ChunkCoord& coord);

    // Call once per frame from the render thread
    void render(const glm::mat4& proj, const glm::mat4& view);

    // Block until the mesh worker is idle (used during initial load)
    void waitIdle() { m_mesher.waitIdle(); }

    bool hasMesh(const ChunkCoord& c) const {
        return m_gpuMeshes.count(c) > 0;
    }

    std::function<void(const ChunkCoord&)> onMeshEvicted;

    void setPlayerChunk(const ChunkCoord& c);

    void setRenderRadius(int r) {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_renderRadius = std::max(0, r);
    }

private:
    // Snapshot the neighbour block data
    std::array<std::shared_ptr<const std::vector<BlockType>>, 6> 
        snapshotNeighbours(const ChunkCoord& coord) const;

    // Enqueue a mesh job for coord
    void enqueueMesh(const ChunkCoord& coord);

    // ---- eligibility / scheduling ----
    void    pumpMeshJobs();
    void    markDirty(const ChunkCoord& c);
    void    refreshRenderRegion();
    bool    withinRenderRange(const ChunkCoord& c) const;
    uint8_t neighborCount(const ChunkCoord& c) const;        // cached lookup
    uint8_t countPresentNeighbors(const ChunkCoord& c) const; // recount from data

    // player pos
    ChunkCoord m_playerChunk{ 0, 0, 0 };
    static const ChunkCoord kDirs[6];

    // Shared data: main thread + network thread
    mutable std::mutex m_dataMutex;
    std::unordered_map<ChunkCoord, std::shared_ptr<std::vector<BlockType>>, ChunkCoordHash> 
        m_chunkData;
    std::unordered_set<ChunkCoord, ChunkCoordHash> m_visibleChunks;

    // Dirty chunks
    std::unordered_map<ChunkCoord, std::chrono::steady_clock::time_point, ChunkCoordHash>
        m_dirty;

    // num neighbors present
    std::unordered_map<ChunkCoord, uint8_t, ChunkCoordHash> m_neighborsPresent;

    ChunkMesher m_mesher;

    // GPU meshes
    std::unordered_map<ChunkCoord, GpuMesh, ChunkCoordHash> m_gpuMeshes;
    std::queue<MeshData> m_readyToUpload;

    std::list<ChunkCoord> m_lru;
    std::unordered_map<ChunkCoord, std::list<ChunkCoord>::iterator, ChunkCoordHash> m_lruPos;
    size_t m_maxResidentMeshes = 4096;
    void touch(const ChunkCoord& c);
    void evictIfNeeded();

    int distSqToPlayer(const ChunkCoord& c) const;

    // ---- tunables ----
    int    m_renderRadius     = WorldCfg::RENDER_DISTANCE;
    float  m_maxPartialWaitMs = 250.f;
    size_t m_targetQueueDepth = 64;
    int    m_uploadsPerFrame  = 16;
};