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

#include <glad.h>
#include <glm/glm.hpp>

#include "world/chunk.hpp"
#include "render/chunk_mesher.hpp"
#include "render/frustum.hpp"

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

    // Called when a chunk is unloaded (main thread only currently)
    void onChunkRemoved(const ChunkCoord& coord);

    // Call once per frame from the render thread
    void render(const glm::mat4& proj, const glm::mat4& view);

    // Block until the mesh worker is idle (used during initial load)
    void waitIdle() { m_mesher.waitIdle(); }

    bool hasMesh(const ChunkCoord& c) const {
        return m_gpuMeshes.count(c) > 0;
    }

    // Fired when the LRU evicts a mesh from GPU memory.
    // Wire this up in the client to call markRendererReleased on the cache.
    std::function<void(const ChunkCoord&)> onMeshEvicted;

private:
    // Snapshot the neighbour block data
    std::array<std::shared_ptr<const std::vector<BlockType>>, 6> 
        snapshotNeighbours(const ChunkCoord& coord) const;

    // Enqueue a mesh job for coord
    void enqueueMesh(const ChunkCoord& coord);

    // Direction table
    static const ChunkCoord kDirs[6];

    // Shared data: main thread + network thread
    mutable std::mutex m_dataMutex;
    std::unordered_map<ChunkCoord, std::shared_ptr<std::vector<BlockType>>, ChunkCoordHash> 
        m_chunkData;

    std::unordered_set<ChunkCoord, ChunkCoordHash> m_visibleChunks;

    // Mesher
    ChunkMesher m_mesher;

    // GPU meshes
    std::unordered_map<ChunkCoord, GpuMesh, ChunkCoordHash> m_gpuMeshes;
    std::list<ChunkCoord> m_lru;  // most-recently-touched
    std::unordered_map<ChunkCoord, std::list<ChunkCoord>::iterator, ChunkCoordHash> m_lruPos;
    size_t m_maxResidentMeshes = 4096;  // vram
    void touch(const ChunkCoord& c);
    void evictIfNeeded();
};