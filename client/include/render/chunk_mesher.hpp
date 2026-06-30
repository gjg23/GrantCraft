#pragma once
// ------------------------------------------------------------------
// client/include/render/chunk_mesher.hpp
// ChunkMesher owns a single background thread that consumes a queue of
// (coord, snapshot) pairs and produces ready-to-upload MeshData objects
// ------------------------------------------------------------------

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include <glm/glm.hpp>
#include "world/chunk.hpp"

struct MeshData {
    std::vector<float>        verts;
    std::vector<unsigned int> idxs;
    ChunkCoord                coord{};
};

struct MeshJob {
    ChunkCoord coord;
    uint32_t   generation;
    int        priorityDistSq;
    std::shared_ptr<const std::vector<BlockType>> center;
    std::array<std::shared_ptr<const std::vector<BlockType>>, 6> neighbours;
};

// ---------------------------------------------------------------------------
// ChunkMesher
// ---------------------------------------------------------------------------
class ChunkMesher {
public:
    using ReadyCallback = std::function<void(MeshData&&)>;

    ChunkMesher();
    explicit ChunkMesher(unsigned threads);
    ~ChunkMesher();

    // Job cycle
    void enqueue(MeshJob job);
    void cancel(const ChunkCoord& coord); 
    void drain(ReadyCallback cb);
    void waitIdle();

    size_t queueSize();

private:
    void workerLoop();
    static MeshData buildMesh(const MeshJob& job);

    struct JobCompare {
        bool operator()(const MeshJob& a, const MeshJob& b) const {
            return a.priorityDistSq > b.priorityDistSq;
        }
    };

    // --- shared between main and worker ---
    std::mutex              m_inMutex;
    std::condition_variable m_inCV;
    std::vector<MeshJob>    m_inHeap;
    // generation tracking: coord -> latest generation enqueued
    std::unordered_map<ChunkCoord, uint32_t, ChunkCoordHash> m_generation;
    int                     m_inFlight = 0;

    std::mutex           m_outMutex;
    std::queue<MeshData> m_outQueue;

    std::atomic<bool> m_running{false};
    std::vector<std::thread> m_workers;
};