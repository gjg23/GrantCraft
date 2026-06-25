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

// GPU async job (tracked inside the pool, main thread polls via pollGPU)
struct GPUJob {
    ChunkCoord   coord;
    BlockType*   blocks = nullptr;  // device pointer
    cudaStream_t stream;
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

    // ---- completed buffer (workers → main) ----
    std::vector<CompletedChunk> completedBuffer;
    std::mutex                  completedMutex;

    // ---- GPU jobs (main-thread only, no mutex needed) ----
    std::vector<GPUJob> gpuJobs;

    // ---- workers ----
    std::vector<std::thread> workers;
    std::atomic<bool>        workerRunning{ false };

    void workerLoop();
    void generateCPU(const ChunkCoord& coord);
    void launchGPU  (const ChunkCoord& coord);
};