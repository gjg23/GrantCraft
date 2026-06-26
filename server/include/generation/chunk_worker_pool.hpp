#pragma once
// =============================================================
// server/include/generation/chunk_worker_pool.hpp
// ONE job: take ChunkCoords in, produce completed Chunks out
// =============================================================

#include "world/chunk.hpp"
#include "world/world_state.hpp"
#include "generation/debug_stats.hpp"

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cuda_runtime.h>

enum class GenBackend {
    CPU,
    GPU,
};

// Completed chunk result pushed by a worker thread
struct CompletedChunk {
    ChunkCoord coord;
    Chunk      data;
};

struct GpuColumn;
struct GPUBuffer {
    BlockType*   d_blocks = nullptr;
    BlockType*   h_blocks = nullptr;
    GpuColumn*   d_cols   = nullptr;
    float*       d_n1     = nullptr;
    float*       d_n2     = nullptr;
    cudaStream_t stream   = nullptr;
};

struct GPUJob {
    ChunkCoord coord;
    int        bufferIdx;
};

class ChunkWorkerPool {
public:
    explicit ChunkWorkerPool(GenBackend backend);
    ~ChunkWorkerPool() { shutdown(); }

    // Start worker threads. Call once at server init.
    void init(int threadCount = -1);  // -1 = hardware_concurrency - 1

    // Stop all workers. Call at server shutdown.
    void shutdown();

    // Submit a chunk coord for generation
    void submit(const ChunkCoord& coord, int priorityDistSq);

    // Drain all chunks completed since the last call
    std::vector<CompletedChunk> drainCompleted();

    // Poll GPU stream completions and move results into completedBuffer
    void pollGPU();

    bool isGPU() const { return backend == GenBackend::GPU; }

    // stats
    WorkerPoolStats getStats() const;

private:
    GenBackend backend;

    // ---- gen queue (main -> workers) ----
    struct PriCoord {
        ChunkCoord coord;
        int        distSq;
        bool operator>(const PriCoord& o) const { return distSq > o.distSq; }
    };

    std::priority_queue<PriCoord, std::vector<PriCoord>, std::greater<PriCoord>> 
                            genQueue;
    std::mutex              genQueueMutex;
    std::condition_variable genQueueCV;

    // ---- GPU: coords staged by workers, launched by main thread only ----
    std::priority_queue<PriCoord, std::vector<PriCoord>, std::greater<PriCoord>>
                            gpuStagingQueue;
    std::mutex              gpuStagingMutex;

    // ---- completed buffer (workers → main) ----
    std::vector<CompletedChunk> completedBuffer;
    std::mutex                  completedMutex;

    // ---- GPU jobs (main-thread only, no mutex needed) ----
    std::vector<GPUJob>     gpuJobs;
    std::vector<GPUBuffer>  gpuBufferPool;
    std::vector<int>        freeBufferIndices;
    int                     MAX_GPU_IN_FLIGHT = 8196;
    void initGPUPool();

    // ---- workers ----
    std::vector<std::thread> workers;
    std::atomic<bool>        workerRunning{ false };

    void workerLoop();
    void generateCPU(const ChunkCoord& coord);
    void launchGPU  (const ChunkCoord& coord, int distSq);
};