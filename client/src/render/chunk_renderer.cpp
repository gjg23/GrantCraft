// client/src/render/chunk_renderer.cpp
#include "render/chunk_renderer.hpp"

#include <glm/gtc/type_ptr.hpp>

// Direction table
const ChunkCoord ChunkRenderer::kDirs[6] = {
    { 1, 0, 0}, {-1, 0, 0},
    { 0, 1, 0}, { 0,-1, 0},
    { 0, 0, 1}, { 0, 0,-1}
};

// ---------------------------------------------------------------------------
// GpuMesh
// ---------------------------------------------------------------------------
void GpuMesh::upload(const std::vector<float>& verts,
                     const std::vector<unsigned int>& idxs) {
    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
    }

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idxs.size() * sizeof(unsigned int)), idxs.data(), GL_DYNAMIC_DRAW);

    constexpr int STRIDE = 6 * static_cast<int>(sizeof(float));
    // attrib 0: position (xyz)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STRIDE, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    // attrib 1: uv
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // attrib 2: faceIdx (0-5) — shader uses this for per-face normals and AO
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, STRIDE, reinterpret_cast<void*>(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    indexCount = static_cast<int>(idxs.size());
}

void GpuMesh::draw() const {
    if (indexCount == 0) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
}

void GpuMesh::release() {
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo);       vbo = 0; }
    if (ebo) { glDeleteBuffers(1, &ebo);       ebo = 0; }
    indexCount = 0;
}

// ---------------------------------------------------------------------------
// ChunkRenderer
// ---------------------------------------------------------------------------
ChunkRenderer::ChunkRenderer()  = default;
ChunkRenderer::~ChunkRenderer() {
    for (auto& [coord, mesh] : m_gpuMeshes) mesh.release();
}

// ---------------------------------------------------------------------------
// onChunkReceived
// ---------------------------------------------------------------------------
void ChunkRenderer::onChunkReceived(const ChunkCoord& coord, std::vector<BlockType> blocks)
{
    std::lock_guard<std::mutex> lk(m_dataMutex);

    // Store (or replace) block data
    m_chunkData[coord] = std::make_shared<std::vector<BlockType>>(std::move(blocks));

    // Mesh this chunk + all 6 face-neighbours that already have data.
    enqueueMesh(coord);
    for (const auto& d : kDirs) {
        ChunkCoord nc{coord.x + d.x, coord.y + d.y, coord.z + d.z};
        if (m_chunkData.count(nc))
            enqueueMesh(nc);
    }
}

// ---------------------------------------------------------------------------
// onChunkRemoved - main thread only
// ---------------------------------------------------------------------------
void ChunkRenderer::onChunkRemoved(const ChunkCoord& coord) {
    {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_chunkData.erase(coord);

        // Re-mesh neighbours so they expose their now-border faces
        for (const auto& d : kDirs) {
            ChunkCoord nc{coord.x + d.x, coord.y + d.y, coord.z + d.z};
            if (m_chunkData.count(nc))
                enqueueMesh(nc);
        }
    }

    auto it = m_gpuMeshes.find(coord);
    if (it != m_gpuMeshes.end()) {
        it->second.release();
        m_gpuMeshes.erase(it);
    }
}

// ---------------------------------------------------------------------------
// render - main thread, called every frame
// ---------------------------------------------------------------------------
void ChunkRenderer::render(const glm::mat4& proj, const glm::mat4& view) {
    // Drain the mesher
    m_mesher.drain([this](MeshData&& md) {
        if (md.idxs.empty()) {
            // Empty mesh
            auto it = m_gpuMeshes.find(md.coord);
            if (it != m_gpuMeshes.end()) {
                it->second.release();
                m_gpuMeshes.erase(it);
            }
            return;
        }
        m_gpuMeshes[md.coord].upload(md.verts, md.idxs);
    });

    // Build frustum from current camera
    Frustum frustum = Frustum::fromMatrix(proj * view);

    // Draw every chunk whose AABB intersects the frustum
    for (auto& [coord, mesh] : m_gpuMeshes) {
        // AABB in world space
        glm::vec3 minP{
            static_cast<float>(coord.x * CHUNK_SIZE),
            static_cast<float>(coord.y * CHUNK_SIZE),
            static_cast<float>(coord.z * CHUNK_SIZE)
        };
        glm::vec3 maxP = minP + glm::vec3(static_cast<float>(CHUNK_SIZE));

        if (!frustum.intersectsAABB(minP, maxP)) continue;

        mesh.draw();
    }

    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
std::array<std::shared_ptr<const std::vector<BlockType>>, 6>
ChunkRenderer::snapshotNeighbours(const ChunkCoord& coord) const {
    std::array<std::shared_ptr<const std::vector<BlockType>>, 6> nb{};
    for (int i = 0; i < 6; ++i) {
        ChunkCoord nc{coord.x + kDirs[i].x, coord.y + kDirs[i].y, coord.z + kDirs[i].z};
        auto it = m_chunkData.find(nc);
        if (it != m_chunkData.end())
            nb[i] = it->second; // shared_ptr copy — no data copy
    }
    return nb;
}

void ChunkRenderer::enqueueMesh(const ChunkCoord& coord) {
    // m_dataMutex must already be held
    auto it = m_chunkData.find(coord);
    if (it == m_chunkData.end()) return;

    MeshJob job;
    job.coord       = coord;
    job.generation  = 0;
    job.center      = it->second;
    job.neighbours  = snapshotNeighbours(coord);

    m_mesher.enqueue(std::move(job));
}