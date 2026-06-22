// server/src/generation/chunk_system.cpp
#include "generation/chunk_system.hpp"
#include "generation/terrain_gen.hpp"
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

    for (auto& snap : snapshots)
        enqueueNeededChunks(worldToChunk(snap.pos));
}

void ChunkSystem::waitForSpawnChunks(int radius) {
    std::vector<ChunkCoord> empty;
    for (int x = -radius; x <= radius; ++x)
        for (int z = -radius; z <= radius; ++z)
            enqueueIfNeeded({x, 0, z}, empty);

    while (true) {
        bool allReady = true;
        {
            std::lock_guard<std::mutex> lk(runtimeMutex);
            for (int x = -radius; x <= radius; ++x)
            for (int z = -radius; z <= radius; ++z)
                if (runtime[{x,0,z}].state != ChunkState::Ready)
                    { allReady = false; break; }
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
    std::vector<std::pair<ChunkCoord, int>> toQueue;
    {
        std::lock_guard<std::mutex> lk(runtimeMutex);
        for (int dx = -renderDistance; dx <= renderDistance; ++dx)
        for (int dz = -renderDistance; dz <= renderDistance; ++dz) {
            ChunkCoord c{ center.x + dx, 0, center.z + dz };
            auto& rt = runtime[c];
            if (rt.state == ChunkState::Unloaded) {
                rt.state = ChunkState::Queued;
                int distSq = dx*dx + dz*dz;
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
        // --- GPU stub ---
        // TODO: launchChunkGenGPU(...) + cudaMemcpy back to chunk.blocks
        // For now falls through to CPU so the server stays functional
        chunk = generateTerrain(coord);
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