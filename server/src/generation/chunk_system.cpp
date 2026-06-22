// server/src/generation/chunk_system.cpp
#include "generation/chunk_system.hpp"
#include "generation/terrain_gen.hpp"

// ======================================================
// Private functions:
// ======================================================

ChunkSystem::ChunkSystem(WorldState& w) : world(w) {}

void ChunkSystem::init() {
    worker_running = true;

    // Leave 1 core for server
    unsigned int threadCount = std::max(1u, std::thread::hardware_concurrency() - 1);

    // start workers for chunk gen
    workers.reserve(threadCount);
    for (unsigned int i = 0; i < threadCount; ++i)
        workers.emplace_back(&ChunkSystem::workerLoop, this);
}

void ChunkSystem::update(const std::vector<PlayerSnapshot>& snapshots) {
    for (auto& snap : snapshots)
        enqueueNeededChunks(worldToChunk(snap.pos));
}

void ChunkSystem::waitForSpawnChunks(int radius) {
    ChunkCoord origin{0, 0, 0};
    // Enqueue spawn area
    for (int x = -radius; x <= radius; ++x)
        for (int z = -radius; z <= radius; ++z)
            enqueueIfNeeded({x, 0, z});

    // Spin until all are Ready
    while (true) {
        bool allReady = true;
        {
            std::lock_guard<std::mutex> lk(runtimeMutex);
            for (int x = -radius; x <= radius; ++x)
                for (int z = -radius; z <= radius; ++z)
                    if (runtime[{x,0,z}].state != ChunkState::Ready)
                        allReady = false;
        }
        if (allReady) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::vector<ChunkCoord> ChunkSystem::getAndClearReadyChunks() {
    std::lock_guard<std::mutex> lk(readyMutex);
    return std::move(readyChunks);
}

void ChunkSystem::shutdown() {
    worker_running = false;
    genQueueCV.notify_all();
    for (auto& t : workers)
        if (t.joinable()) t.join();
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
    std::vector<ChunkCoord> toQueue;
    {
        std::lock_guard<std::mutex> lk(runtimeMutex);
        for (int dx = -renderDistance; dx <= renderDistance; ++dx)
            for (int dz = -renderDistance; dz <= renderDistance; ++dz) {
                ChunkCoord c{ center.x + dx, 0, center.z + dz };
                auto& rt = runtime[c];
                if (rt.state == ChunkState::Unloaded) {
                    rt.state = ChunkState::Queued;
                    toQueue.push_back(c);
                }
            }
    }

    if (!toQueue.empty()) {
        std::lock_guard<std::mutex> qlk(genQueueMutex);
        for (auto& c : toQueue) genQueue.push(c);
        genQueueCV.notify_all();
    }
}

// Enqueue a single chunk
void ChunkSystem::enqueueIfNeeded(const ChunkCoord& coord) {
    // Try to enqueue
    {
        std::lock_guard<std::mutex> lk(runtimeMutex);
        auto& rt = runtime[coord];
        if (rt.state == ChunkState::Unloaded) {
            // Setting to queued
            rt.state = ChunkState::Queued;
        } else return;  // Not ready to queue chunk
    }
 
    // Queue chunk so push to queue
    {
        std::lock_guard<std::mutex> qlk(genQueueMutex);
        genQueue.push(coord);
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
            coord = genQueue.front();
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
    // In future - branch here to use CPU vs GPU generation algorithm
    auto chunk = generateTerrain(coord);

    // Write into world state
    {
        std::lock_guard<std::mutex> lk(runtimeMutex);
        world.chunks[coord] = std::move(chunk);
        runtime[coord].state = ChunkState::Ready;
    }

    // Notify server to send this chunk to relevant players
    {
        std::lock_guard<std::mutex> lk(readyMutex);
        readyChunks.push_back(coord);
    }
}