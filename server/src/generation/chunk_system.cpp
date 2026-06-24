// server/src/generation/chunk_system.cpp
#include "generation/chunk_system.hpp"
#include "generation/terrain_gen.hpp"
#include "generation/gpu_terrain/gpu_launch.cuh"
#include <cmath>
#include <algorithm>

// ======================================================
// Private functions:
// ======================================================

ChunkSystem::ChunkSystem(WorldState& w, GenBackend backend) : world(w), backend(backend) {}

void ChunkSystem::init() {
    worker_running = true;

    // Leave 1 core for server
    unsigned int threadCount = std::max(1u, std::thread::hardware_concurrency() - 1);

    // start workers for chunk gen
    workers.reserve(threadCount);
    for (unsigned int i = 0; i < threadCount; ++i)
        workers.emplace_back(&ChunkSystem::workerLoop, this);
}

void ChunkSystem::shutdown() {
    worker_running = false;
    genQueueCV.notify_all();
    for (auto& t : workers)
        if (t.joinable()) t.join();
}

int ChunkSystem::closestDistSq(const ChunkCoord& coord, const std::vector<ChunkCoord>& centers) {
    int best = INT_MAX;
    for (auto& c : centers) {
        int dx = coord.x - c.x;
        int dz = coord.z - c.z;
        best = std::min(best, dx*dx + dz*dz);
    }
    return best;
}

void ChunkSystem::update(const std::vector<PlayerSnapshot>& snapshots) {
    // Build centers locally — no need to store as member state
    std::vector<ChunkCoord> centers;
    centers.reserve(snapshots.size());
    for (auto& snap : snapshots)
        centers.push_back(worldToChunk(snap.pos));

    for (auto& center : centers)
        enqueueNeededChunks(center);

    if (backend == GenBackend::GPU)
        pollGPU();
}

void ChunkSystem::waitForSpawnChunks(int radius) {
    std::vector<ChunkCoord> empty;
    int rSq = radius * radius;
    for (int dx = -radius; dx <= radius; ++dx)
    for (int dy = -radius; dy <= radius; ++dy)
    for (int dz = -radius; dz <= radius; ++dz) {
        ChunkCoord c{dx, dy, dz};
        enqueueIfNeeded(c, empty);
    }

    while (true) {
        if (backend == GenBackend::GPU)
            pollGPU();
        bool allReady = true;
        {
            std::lock_guard<std::mutex> lk(runtimeMutex);
            for (int dx = -radius; dx <= radius && allReady; ++dx)
            for (int dy = -radius; dy <= radius && allReady; ++dy)
            for (int dz = -radius; dz <= radius && allReady; ++dz) {
                ChunkCoord c{dx, dy, dz};
                if (runtime[c].state != ChunkState::Ready)
                    allReady = false;
            }
        }
        if (allReady) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::vector<ChunkCoord> ChunkSystem::getAndClearReadyChunks() {
    std::lock_guard<std::mutex> lk(readyMutex);
    return std::move(readyChunks);
}


// ======================================================
// Public functions:
// ======================================================

ChunkCoord ChunkSystem::worldToChunk(const glm::vec3& pos) {
    return { (int)std::floor(pos.x / CHUNK_SIZE),
             (int)std::floor(pos.y / CHUNK_SIZE),
             (int)std::floor(pos.z / CHUNK_SIZE) };
}

// Enqueue many chunks based on render distance
void ChunkSystem::enqueueNeededChunks(const ChunkCoord& center) {
    int r   = renderDistance;
    int rSq = r * r;
    
    std::vector<std::pair<ChunkCoord, int>> toQueue;
    {
        std::lock_guard<std::mutex> lk(runtimeMutex);
        for (int dx = -r; dx <= r; ++dx)
        for (int dy = -r; dy <= r; ++dy)
        for (int dz = -r; dz <= r; ++dz) {
            ChunkCoord c{ center.x + dx, center.y + dy, center.z + dz };
            auto& rt = runtime[c];
            if (rt.state == ChunkState::Unloaded) {
                rt.state = ChunkState::Queued;
                int distSq = dx*dx + dy*dy + dz*dz;
                toQueue.push_back({c, distSq});
            }
        }
    }

    if (!toQueue.empty()) {
        std::lock_guard<std::mutex> qlk(genQueueMutex);
        for (auto& [c, d] : toQueue)
            genQueue.push({ c, d });
        genQueueCV.notify_all();
    }
}

// Enqueue a single chunk
void ChunkSystem::enqueueIfNeeded(const ChunkCoord& coord, const std::vector<ChunkCoord>& centers) {
    {
        std::lock_guard<std::mutex> lk(runtimeMutex);
        auto& rt = runtime[coord];
        if (rt.state != ChunkState::Unloaded) return;
        rt.state = ChunkState::Queued;
    }
    {
        std::lock_guard<std::mutex> qlk(genQueueMutex);
        genQueue.push({ coord, closestDistSq(coord, centers) });
    }
    genQueueCV.notify_one();
}

// Checks if chunks are needed to create and dispatches generateChunk next
void ChunkSystem::workerLoop() {
    while (true) {
        ChunkCoord coord;
        {
            // Wait for a chunk needed to generate
            std::unique_lock<std::mutex> lk(genQueueMutex);
            genQueueCV.wait(lk, [this]{
                return !genQueue.empty() || !worker_running;
            });

            if (!worker_running && genQueue.empty()) break;

            // Chunk needed -> remove from queue & generate
            coord = genQueue.top().coord;
            genQueue.pop();
        }
        generateChunk(coord);
    }
}

void ChunkSystem::generateChunk(const ChunkCoord& coord) {
    // Mark as generating
    {
        std::lock_guard<std::mutex> lk(runtimeMutex);
        runtime[coord].state = ChunkState::Generating;
    }

    // Do the actual work (no locks held during generation)
    Chunk chunk;
    if (backend == GenBackend::GPU) {
        GPUJob job;
        job.coord = coord;

        cudaMalloc(&job.blocks,  CHUNK_VOLUME * sizeof(BlockType));;
        cudaStreamCreate(&job.stream);

        launchChunkGenGPU(
            job.blocks,
            coord.x * CHUNK_SIZE,
            coord.y * CHUNK_SIZE,
            coord.z * CHUNK_SIZE,
            job.stream
        );

        {
            std::lock_guard<std::mutex> lk(gpuJobsMutex);
            gpuJobs.push_back(std::move(job));
        }
        return;
    } else {
        chunk = generateTerrain(coord);
    }

    // Write into world state
    {
        std::lock_guard<std::mutex> lk(runtimeMutex);
        Chunk& dst = world.insertChunk(coord);
        dst = std::move(chunk);
        runtime[coord].state = ChunkState::Ready;
    }

    // Notify server to send this chunk to relevant players
    {
        std::lock_guard<std::mutex> lk(readyMutex);
        readyChunks.push_back(coord);
    }
}

// poll gpu completion
void ChunkSystem::pollGPU() {
    // Collect completed jobs without holding gpuJobsMutex during publish
    struct CompletedJob {
        ChunkCoord coord;
        Chunk chunk;
    };
    std::vector<CompletedJob> completed;

    {
        std::lock_guard<std::mutex> lk(gpuJobsMutex);
        for (auto it = gpuJobs.begin(); it != gpuJobs.end(); ) {
            cudaError_t status = cudaStreamQuery(it->stream);

            if (status == cudaSuccess) {
                Chunk chunk;
                chunk.blocks.resize(CHUNK_VOLUME);
                cudaMemcpy(chunk.blocks.data(), it->blocks,
                           CHUNK_VOLUME * sizeof(BlockType),
                           cudaMemcpyDeviceToHost);

                cudaFree(it->blocks);
                it->blocks = nullptr;
                cudaStreamDestroy(it->stream);

                completed.push_back({ it->coord, std::move(chunk) });
                it = gpuJobs.erase(it);

            } else if (status == cudaErrorNotReady) {
                ++it;
            } else {
                fprintf(stderr, "[pollGPU] stream error on chunk (%d,%d,%d): %s\n",
                    it->coord.x, it->coord.y, it->coord.z,
                    cudaGetErrorString(status));
                cudaFree(it->blocks);
                cudaStreamDestroy(it->stream);
                {
                    std::lock_guard<std::mutex> rlk(runtimeMutex);
                    runtime[it->coord].state = ChunkState::Unloaded;
                }
                it = gpuJobs.erase(it);
            }
        }
    } // gpuJobsMutex released here

    // Now publish without holding gpuJobsMutex
    for (auto& job : completed) {
        {
            std::lock_guard<std::mutex> lk(runtimeMutex);
            world.insertChunk(job.coord) = std::move(job.chunk);
            runtime[job.coord].state = ChunkState::Ready;
        }
        {
            std::lock_guard<std::mutex> lk(readyMutex);
            readyChunks.push_back(job.coord);
        }
    }
}