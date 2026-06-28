// client/src/render/chunk_mesher.cpp
#include "render/chunk_mesher.hpp"
#include "world/block.hpp"   // getTextureRow(), isOpaque()

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// Vertex layout helpers
// ---------------------------------------------------------------------------
static const float TILEV = 1.0f / 19.0f; // texture atlas rows

static void addFace(std::vector<float>& verts, std::vector<unsigned int>& idxs,
                    float x0,float y0,float z0,
                    float x1,float y1,float z1,
                    float x2,float y2,float z2,
                    float x3,float y3,float z3,
                    float u0,float v0, float u1,float v1,
                    float u2,float v2, float u3,float v3,
                    float faceIdx)
{
    auto base = static_cast<unsigned int>(verts.size() / 6);
    verts.insert(verts.end(), {
        x0,y0,z0, u0,v0, faceIdx,
        x1,y1,z1, u1,v1, faceIdx,
        x2,y2,z2, u2,v2, faceIdx,
        x3,y3,z3, u3,v3, faceIdx,
    });
    idxs.insert(idxs.end(), {base, base+1, base+2,  base, base+2, base+3});
}


// ---------------------------------------------------------------------------
// Block access helpers
// ---------------------------------------------------------------------------
static inline int blockIndex(int x, int y, int z) {
    // layout: x + CHUNK_SIZE*(z + CHUNK_SIZE*y)
    return x + CHUNK_SIZE * (z + CHUNK_SIZE * y);
}

static BlockType getLocal(const std::vector<BlockType>& data, int x, int y, int z) {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
        return BlockType::Air;
    return data[blockIndex(x, y, z)];
}

// Checks whether face (x,y,z)+delta should be drawn
// Uses neighbour snapshots for cross-boundary lookups
// neighbours: [0]=+X,[1]=-X,[2]=+Y,[3]=-Y,[4]=+Z,[5]=-Z
static bool isFaceVisible(
    const std::vector<BlockType>& center,
    const std::array<std::shared_ptr<const std::vector<BlockType>>, 6>& neighbours,
    int x, int y, int z,
    int dx, int dy, int dz)
{
    int nx = x + dx, ny = y + dy, nz = z + dz;

    // Inside this chunk — fast path
    if (nx >= 0 && nx < CHUNK_SIZE && ny >= 0 && ny < CHUNK_SIZE && nz >= 0 && nz < CHUNK_SIZE)
        return !isOpaque(getLocal(center, nx, ny, nz));

    // Crossed a chunk boundary
    int faceIdx = -1;
    int lx = nx, ly = ny, lz = nz;

    if      (nx < 0)           { faceIdx = 1; lx = CHUNK_SIZE - 1; }
    else if (nx >= CHUNK_SIZE) { faceIdx = 0; lx = 0; }
    else if (ny < 0)           { faceIdx = 3; ly = CHUNK_SIZE - 1; }
    else if (ny >= CHUNK_SIZE) { faceIdx = 2; ly = 0; }
    else if (nz < 0)           { faceIdx = 5; lz = CHUNK_SIZE - 1; }
    else if (nz >= CHUNK_SIZE) { faceIdx = 4; lz = 0; }

    const auto& nb = neighbours[faceIdx];
    if (!nb) return true; // no neighbour = exposed

    return !isOpaque(getLocal(*nb, lx, ly, lz));
}

// ---------------------------------------------------------------------------
// Core mesh build
// ---------------------------------------------------------------------------
MeshData ChunkMesher::buildMesh(const MeshJob& job) {
    MeshData out;
    out.coord = job.coord;

    if (!job.center) return out;
    const auto& center = *job.center;

    auto& verts = out.verts;
    auto& idxs  = out.idxs;
    verts.reserve(4096);
    idxs.reserve(2048);

    // World-space origin of this chunk
    float ox = static_cast<float>(job.coord.x * CHUNK_SIZE);
    float oy = static_cast<float>(job.coord.y * CHUNK_SIZE);
    float oz = static_cast<float>(job.coord.z * CHUNK_SIZE);

    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    for (int ly = 0; ly < CHUNK_SIZE; ++ly)
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
        BlockType b = getLocal(center, lx, ly, lz);
        if (!isOpaque(b)) continue;

        float fx = ox + static_cast<float>(lx);
        float fy = oy + static_cast<float>(ly);
        float fz = oz + static_cast<float>(lz);

        int   row  = getTextureRow(b);
        float vTop = row * TILEV;
        float vBot = (row + 1) * TILEV;

        auto visible = [&](int dx, int dy, int dz) {
            return isFaceVisible(center, job.neighbours, lx, ly, lz, dx, dy, dz);
        };

        // +X face
        if (visible(1,0,0))
            addFace(verts,idxs,
                fx+1,fy,  fz,   fx+1,fy+1,fz,   fx+1,fy+1,fz+1, fx+1,fy,  fz+1,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 0.0f);
        // -X face
        if (visible(-1,0,0))
            addFace(verts,idxs,
                fx,fy,  fz+1, fx,fy+1,fz+1, fx,fy+1,fz,   fx,fy,  fz,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 1.0f);
        // +Y face (top)
        if (visible(0,1,0))
            addFace(verts,idxs,
                fx,  fy+1,fz,   fx,  fy+1,fz+1, fx+1,fy+1,fz+1, fx+1,fy+1,fz,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 2.0f);
        // -Y face (bottom)
        if (visible(0,-1,0))
            addFace(verts,idxs,
                fx,  fy,fz+1, fx,  fy,fz,   fx+1,fy,fz,   fx+1,fy,fz+1,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 3.0f);
        // +Z face
        if (visible(0,0,1))
            addFace(verts,idxs,
                fx+1,fy,  fz+1, fx+1,fy+1,fz+1, fx,  fy+1,fz+1, fx,  fy,  fz+1,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 4.0f);
        // -Z face
        if (visible(0,0,-1))
            addFace(verts,idxs,
                fx,  fy,  fz,   fx,  fy+1,fz,   fx+1,fy+1,fz,   fx+1,fy,  fz,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 5.0f);
    }

    return out;
}

// ---------------------------------------------------------------------------
// ChunkMesher lifecycle
// ---------------------------------------------------------------------------
ChunkMesher::ChunkMesher()
    : ChunkMesher(
        std::max(1u, std::thread::hardware_concurrency() > 1
        ? std::thread::hardware_concurrency() - 1 
        : 1u)
    ) {}

ChunkMesher::ChunkMesher(unsigned threads) {
    m_running = true;
    for (unsigned i = 0; i < threads; ++i)
        m_workers.emplace_back(&ChunkMesher::workerLoop, this);
}

ChunkMesher::~ChunkMesher() {
    {
        std::lock_guard<std::mutex> lk(m_inMutex);
        m_running = false;
    }
    m_inCV.notify_all();
    for (auto& t : m_workers)
        if (t.joinable()) t.join();
}

void ChunkMesher::enqueue(MeshJob job) {
    std::lock_guard<std::mutex> lk(m_inMutex);
    job.generation = ++m_generation[job.coord];
    m_inHeap.push_back(std::move(job));
    std::push_heap(m_inHeap.begin(), m_inHeap.end(), JobCompare{});
    m_inCV.notify_one();
}

void ChunkMesher::cancel(const ChunkCoord& coord) {
    std::lock_guard<std::mutex> lk(m_inMutex);
    ++m_generation[coord];
}

void ChunkMesher::drain(ReadyCallback cb) {
    std::queue<MeshData> local;
    {
        std::lock_guard<std::mutex> lk(m_outMutex);
        std::swap(local, m_outQueue);
    }
    while (!local.empty()) {
        cb(std::move(local.front()));
        local.pop();
    }
}

void ChunkMesher::waitIdle() {
    while (true) {
        {
            std::lock_guard<std::mutex> lk(m_inMutex);
            if (m_inHeap.empty() && m_inFlight == 0) return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

size_t ChunkMesher::queueSize() {
    std::lock_guard<std::mutex> lk(m_inMutex);
    return m_inHeap.size() + static_cast<size_t>(m_inFlight);
}

void ChunkMesher::workerLoop() {
    while (true) {
        MeshJob job;
        {
            std::unique_lock<std::mutex> lk(m_inMutex);
            m_inCV.wait(lk, [this]{ return !m_inHeap.empty() || !m_running; });
            if (!m_running && m_inHeap.empty()) return;

            std::pop_heap(m_inHeap.begin(), m_inHeap.end(), JobCompare{});
            job = std::move(m_inHeap.back());
            m_inHeap.pop_back();

            // Skip if a newer enqueue/cancel superseded this job.
            auto it = m_generation.find(job.coord);
            if (it != m_generation.end() && it->second != job.generation)
                continue;

            ++m_inFlight;
        }

        MeshData result = buildMesh(job);

        {
            std::lock_guard<std::mutex> lk(m_outMutex);
            m_outQueue.push(std::move(result));
        }
        {
            std::lock_guard<std::mutex> lk(m_inMutex);
            --m_inFlight;
        }
    }
}