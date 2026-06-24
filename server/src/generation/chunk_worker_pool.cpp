// server/src/generation/chunk_worker_pool.cpp

#include "generation/chunk_worker_pool.hpp"
#include "generation/terrain_gen.hpp"
#include "generation/gpu_terrain/gpu_launch.cuh"
#include "world/chunk.hpp"

#include <algorithm>
#include <cstdio>

ChunkWorkerPool::ChunkWorkerPool(GenBackend backend) : backend(backend) {}

void ChunkWorkerPool::init(int threadCount) {
    workerRunning = true;

    int count = (threadCount > 0)
        ? threadCount
        : static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1));

    workers.reserve(count);
    for (int i = 0; i < count; ++i)
        workers.emplace_back(&ChunkWorkerPool::workerLoop, this);
}

void ChunkWorkerPool::shutdown() {
    workerRunning = false;
    genQueueCV.notify_all();
    for (auto& t : workers)
        if (t.joinable()) t.join();
    workers.clear();
}

void ChunkWorkerPool::submit(const ChunkCoord& coord, int priorityDistSq) {
    {
        std::lock_guard<std::mutex> lk(genQueueMutex);
        genQueue.push({ coord, priorityDistSq });
    }
    genQueueCV.notify_one();
}

std::vector<CompletedChunk> ChunkWorkerPool::drainCompleted() {
    std::vector<CompletedChunk> out;
    {
        std::lock_guard<std::mutex> lk(completedMutex);
        out = std::move(completedBuffer);
        completedBuffer.clear();
    }
    return out;
}

void ChunkWorkerPool::workerLoop() {
    while (true) {
        ChunkCoord coord;
        {
            std::unique_lock<std::mutex> lk(genQueueMutex);
            genQueueCV.wait(lk, [this] {
                return !genQueue.empty() || !workerRunning;
            });

            if (!workerRunning && genQueue.empty())
                break;

            coord = genQueue.top().coord;
            genQueue.pop();
        }
        // Lock released before any generation work

        if (backend == GenBackend::GPU)
            launchGPU(coord);
        else
            generateCPU(coord);
    }
}

void ChunkWorkerPool::generateCPU(const ChunkCoord& coord) {
    // terrain_gen.hpp — pure function, no shared state
    Chunk chunk = generateTerrain(coord);

    std::lock_guard<std::mutex> lk(completedMutex);
    completedBuffer.push_back({ coord, std::move(chunk) });
}

void ChunkWorkerPool::launchGPU(const ChunkCoord& coord) {
    GPUJob job;
    job.coord = coord;

    if (cudaMalloc(&job.blocks, CHUNK_VOLUME * sizeof(BlockType)) != cudaSuccess) {
        fprintf(stderr, "[ChunkWorkerPool] cudaMalloc failed for (%d,%d,%d)\n",
                coord.x, coord.y, coord.z);
        return;
    }
    cudaStreamCreate(&job.stream);

    launchChunkGenGPU(
        job.blocks,
        coord.x * CHUNK_SIZE,
        coord.y * CHUNK_SIZE,
        coord.z * CHUNK_SIZE,
        job.stream
    );

    {
        std::lock_guard<std::mutex> lk(completedMutex); // reuse completedMutex — both are infrequent
        gpuJobs.push_back(std::move(job));
    }
}

// pollGPU — called from the main thread inside ChunkDispatch
void ChunkWorkerPool::pollGPU() {
    if (backend != GenBackend::GPU) return;

    // Snapshot gpuJobs under the mutex, then release before memcpy
    std::vector<GPUJob> snapshot;
    {
        std::lock_guard<std::mutex> lk(completedMutex);
        snapshot = std::move(gpuJobs);
        gpuJobs.clear();
    }

    std::vector<GPUJob> stillPending;

    for (auto& job : snapshot) {
        cudaError_t status = cudaStreamQuery(job.stream);

        if (status == cudaSuccess) {
            Chunk chunk;
            chunk.blocks.resize(CHUNK_VOLUME);
            cudaMemcpy(
                chunk.blocks.data(), job.blocks,
                CHUNK_VOLUME * sizeof(BlockType),
                cudaMemcpyDeviceToHost
            );
            cudaFree(job.blocks);
            cudaStreamDestroy(job.stream);

            std::lock_guard<std::mutex> lk(completedMutex);
            completedBuffer.push_back({ job.coord, std::move(chunk) });

        } else if (status == cudaErrorNotReady) {
            stillPending.push_back(job);   // put back

        } else {
            fprintf(stderr, "[ChunkWorkerPool] GPU stream error on (%d,%d,%d): %s\n",
                    job.coord.x, job.coord.y, job.coord.z,
                    cudaGetErrorString(status));
            cudaFree(job.blocks);
            cudaStreamDestroy(job.stream);
            // Chunk simply never appears in completedBuffer
            // ChunkRegistry stays in Generating state
            // Future: could push a "failed" signal to retry
        }
    }

    // Put unfinished jobs back
    if (!stillPending.empty()) {
        std::lock_guard<std::mutex> lk(completedMutex);
        for (auto& j : stillPending)
            gpuJobs.push_back(std::move(j));
    }
}

// stats for debug
WorkerPoolStats ChunkWorkerPool::getStats() const {
    WorkerPoolStats s;
    {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(genQueueMutex));
        s.queueDepth = static_cast<uint32_t>(genQueue.size());
    }
    {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(completedMutex));
        s.gpuInFlight = static_cast<uint32_t>(gpuJobs.size());
    }
    return s;
}