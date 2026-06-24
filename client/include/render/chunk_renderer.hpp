#pragma once
// ------------------------------------------------------------------
// client/include/render/chunk_renderer.hpp
// ------------------------------------------------------------------

#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

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

    // Called when a chunk is unloaded (main thread only currently)
    void onChunkRemoved(const ChunkCoord& coord);

    // Call once per frame from the render thread
    void render(const glm::mat4& proj, const glm::mat4& view);

    // Block until the mesh worker is idle (used during initial load)
    void waitIdle() { m_mesher.waitIdle(); }

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

    // Mesher
    ChunkMesher m_mesher;

    // GPU meshes
    std::unordered_map<ChunkCoord, GpuMesh, ChunkCoordHash> m_gpuMeshes;
};