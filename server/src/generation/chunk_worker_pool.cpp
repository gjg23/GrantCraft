// server/src/generation/chunk_worker_pool.cpp

#include "generation/chunk_worker_pool.hpp"
#include "generation/terrain_gen.hpp"
#include "generation/gpu_terrain/gpu_launch.cuh"
#include "generation/gpu_terrain/kernel.cuh"
#include "generation/terrain_detail.hpp"
#include "world/chunk.hpp"

#include <algorithm>
#include <cstdio>

ChunkWorkerPool::ChunkWorkerPool(GenBackend backend) : backend(backend) {}

void ChunkWorkerPool::init(int threadCount) {
    workerRunning = true;

    if (backend == GenBackend::GPU) {
        return;
    }

    int count = (threadCount > 0)
        ? threadCount
        : static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1));

    workers.reserve(count);
    for (int i = 0; i < count; ++i)
        workers.emplace_back(&ChunkWorkerPool::workerLoop, this);
}

void ChunkWorkerPool::initGPUPool() {
    return;
}

void ChunkWorkerPool::shutdown() {
    // cpu
    workerRunning = false;
    genQueueCV.notify_all();
    for (auto& t : workers)
        if (t.joinable()) t.join();
    workers.clear();

    // GPU
    for (auto& job : gpuJobs) {
        GPUBuffer& buf = gpuBufferPool[job.bufferIdx];
        cudaStreamSynchronize(buf.stream);
    }
    gpuJobs.clear();

    for (auto& buf : gpuBufferPool) {
        if (buf.stream)   cudaStreamDestroy(buf.stream);
        if (buf.d_blocks) cudaFree(buf.d_blocks);
        if (buf.h_blocks) cudaFreeHost(buf.h_blocks);
        if (buf.d_cols)   cudaFree(buf.d_cols);
        if (buf.d_n1)     cudaFree(buf.d_n1);
        if (buf.d_n2)     cudaFree(buf.d_n2);
    }
    gpuBufferPool.clear();
    freeBufferIndices.clear();
}

void ChunkWorkerPool::submit(const ChunkCoord& coord, int priorityDistSq) {
    if (backend == GenBackend::GPU) {
        std::lock_guard<std::mutex> lk(gpuStagingMutex);
        gpuStagingQueue.push({ coord, priorityDistSq });
        return;
    }
    {
        std::lock_guard<std::mutex> lk(genQueueMutex);
        genQueue.push({ coord, priorityDistSq });
    }
    genQueueCV.notify_one();
}

std::vector<CompletedChunk> ChunkWorkerPool::drainCompleted() {
    std::lock_guard<std::mutex> lk(completedMutex);
    return std::move(completedBuffer);
}

void ChunkWorkerPool::workerLoop() {
    if (backend != GenBackend::CPU) return;
    while (workerRunning.load(std::memory_order_relaxed)) {
        PriCoord item;
        {
            std::unique_lock<std::mutex> lock(genQueueMutex);
            genQueueCV.wait(lock, [this] {
                return !genQueue.empty() || !workerRunning;
            });
            if (!workerRunning && genQueue.empty()) return;
            item = genQueue.top();
            genQueue.pop();
        }

        generateCPU(item.coord);
    }
}

void ChunkWorkerPool::generateCPU(const ChunkCoord& coord) {
    // terrain_gen.hpp — pure function, no shared state
    Chunk chunk = generateTerrain(coord);

    std::lock_guard<std::mutex> lk(completedMutex);
    completedBuffer.push_back({ coord, std::move(chunk) });
}

void ChunkWorkerPool::launchGPU(const ChunkCoord& coord, int distSq) {
    int idx;

    if (!freeBufferIndices.empty()) {
        idx = freeBufferIndices.back();
        freeBufferIndices.pop_back();
    } else {
        GPUBuffer newBuf;

        const size_t latCount = (size_t)CAVE_LX * CAVE_LY * CAVE_LZ;

        cudaError_t err = cudaMalloc(&newBuf.d_blocks, CHUNK_VOLUME * sizeof(BlockType));
        if (err != cudaSuccess) {
            fprintf(stderr, "[GPU] cudaMalloc d_blocks failed: %s\n", cudaGetErrorString(err));
            return;
        }
        err = cudaHostAlloc(&newBuf.h_blocks, CHUNK_VOLUME * sizeof(BlockType), cudaHostAllocDefault);
        if (err != cudaSuccess) {
            fprintf(stderr, "[GPU] cudaHostAlloc failed: %s\n", cudaGetErrorString(err));
            cudaFree(newBuf.d_blocks);
            return;
        }
        err = cudaMalloc(&newBuf.d_cols, (size_t)CHUNK_SIZE * CHUNK_SIZE * sizeof(GpuColumn));
        if (err != cudaSuccess) {
            fprintf(stderr, "[GPU] cudaMalloc d_cols failed: %s\n", cudaGetErrorString(err));
            cudaFreeHost(newBuf.h_blocks); cudaFree(newBuf.d_blocks);
            return;
        }
        err = cudaMalloc(&newBuf.d_n1, latCount * sizeof(float));
        if (err != cudaSuccess) {
            fprintf(stderr, "[GPU] cudaMalloc d_n1 failed: %s\n", cudaGetErrorString(err));
            cudaFree(newBuf.d_cols); cudaFreeHost(newBuf.h_blocks); cudaFree(newBuf.d_blocks);
            return;
        }
        err = cudaMalloc(&newBuf.d_n2, latCount * sizeof(float));
        if (err != cudaSuccess) {
            fprintf(stderr, "[GPU] cudaMalloc d_n2 failed: %s\n", cudaGetErrorString(err));
            cudaFree(newBuf.d_n1); cudaFree(newBuf.d_cols);
            cudaFreeHost(newBuf.h_blocks); cudaFree(newBuf.d_blocks);
            return;
        }

        cudaStreamCreateWithFlags(&newBuf.stream, cudaStreamNonBlocking);

        idx = static_cast<int>(gpuBufferPool.size());
        gpuBufferPool.push_back(newBuf);
    }

    GPUBuffer& buf = gpuBufferPool[idx];
    launchChunkGenGPU(buf.d_blocks, buf.d_cols, buf.d_n1, buf.d_n2,
                      coord.x, coord.y, coord.z, buf.stream);
    cudaMemcpyAsync(buf.h_blocks, buf.d_blocks,
                    CHUNK_VOLUME * sizeof(BlockType),
                    cudaMemcpyDeviceToHost, buf.stream);

    gpuJobs.push_back({ coord, idx });
}

// pollGPU — called from the main thread inside ChunkDispatch
void ChunkWorkerPool::pollGPU() {
    if (backend != GenBackend::GPU) return;

    // ---- check steams for completion ----
    for (auto it = gpuJobs.begin(); it != gpuJobs.end(); ) {
        GPUBuffer& buf = gpuBufferPool[it->bufferIdx];
        cudaError_t status = cudaStreamQuery(buf.stream);

        if (status == cudaSuccess) {
            // Stream finished — copy device data to host and free
            Chunk chunk;
            chunk.blocks.assign(buf.h_blocks, buf.h_blocks + CHUNK_VOLUME);
            freeBufferIndices.push_back(it->bufferIdx);
            {
                std::lock_guard<std::mutex> lk(completedMutex);
                completedBuffer.push_back({ it->coord, std::move(chunk) });
            }
            it = gpuJobs.erase(it);
        } else if (status == cudaErrorNotReady) {
            ++it;
        } else {
            fprintf(stderr, "[GPU] Stream error for chunk (%d,%d,%d): %s\n",
                    it->coord.x, it->coord.y, it->coord.z,
                    cudaGetErrorString(status));
            freeBufferIndices.push_back(it->bufferIdx);
            it = gpuJobs.erase(it);
        }
    }

    // ---- launch staged jobs ----
    std::vector<PriCoord> tolaunch;
    {
        std::lock_guard<std::mutex> lk(gpuStagingMutex);
        while (!gpuStagingQueue.empty()) {
            tolaunch.push_back(gpuStagingQueue.top());
            gpuStagingQueue.pop();
        }
    }

    for (const auto& item : tolaunch) {
        launchGPU(item.coord, item.distSq);
    }
}

// stats for debug
WorkerPoolStats ChunkWorkerPool::getStats() const {
    WorkerPoolStats s;
    {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(genQueueMutex));
        s.queueDepth = static_cast<uint32_t>(genQueue.size());
    }
    s.gpuInFlight = static_cast<uint32_t>(gpuBufferPool.size() - freeBufferIndices.size());
    return s;
}