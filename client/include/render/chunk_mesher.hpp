#pragma once
// ------------------------------------------------------------------
// client/include/render/chunk_mesher.hpp
// ChunkMesher owns a single background thread that consumes a queue of
// (coord, snapshot) pairs and produces ready-to-upload MeshData objects.
// ------------------------------------------------------------------

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "world/chunk.hpp"   // ChunkCoord, ChunkCoordHash, BlockType, CHUNK_SIZE

// MeshData
struct MeshData {
    std::vector<float>        verts;
    std::vector<unsigned int> idxs;
    ChunkCoord                coord{};
};

// MeshJob push into the worker queue
struct MeshJob {
    ChunkCoord coord;
    uint32_t   generation; // bumped each time the coord is re-queued

    // Block data snapshots (center + 6 face neighbours, null if absent)
    std::shared_ptr<const std::vector<BlockType>> center;
    std::array<std::shared_ptr<const std::vector<BlockType>>, 6> neighbours;
    // neighbours[0]=+X, [1]=-X, [2]=+Y, [3]=-Y, [4]=+Z, [5]=-Z
};

// ---------------------------------------------------------------------------
// ChunkMesher
// ---------------------------------------------------------------------------
class ChunkMesher {
public:
    // callback invoked on the *calling* thread when drain() is polled —
    // i.e. always on the main/render thread.  Safe to call glBufferData here.
    using ReadyCallback = std::function<void(MeshData&&)>;

    ChunkMesher();
    ~ChunkMesher();

    // Push a job.  If coord is already queued, bump its generation so the old
    // job is discarded when the worker reaches it.
    void enqueue(MeshJob job);

    // Invalidate any queued / in-flight result for this chunk.
    void cancel(const ChunkCoord& coord); 

    // Call once per frame from the render thread.
    // Invokes cb for every MeshData that the worker has finished since last drain.
    void drain(ReadyCallback cb);

    // Block until the worker queue is empty (used during initial load).
    void waitIdle();

private:
    void workerLoop();
    static MeshData buildMesh(const MeshJob& job);

    // --- shared between main and worker ---
    std::mutex              m_inMutex;
    std::condition_variable m_inCV;
    std::queue<MeshJob>     m_inQueue;
    // generation tracking: coord -> latest generation enqueued
    std::unordered_map<ChunkCoord, uint32_t, ChunkCoordHash> m_generation;

    std::mutex           m_outMutex;
    std::queue<MeshData> m_outQueue;

    std::atomic<bool> m_running{false};
    std::thread       m_worker;
};