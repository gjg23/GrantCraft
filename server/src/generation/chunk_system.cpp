// server/src/generation/chunk_system.cpp
#include "generation/chunk_system.hpp"
#include "generation/terrain_gen.hpp"

ChunkSystem::ChunkSystem(WorldState& w) : world(w) {}

void ChunkSystem::init() {
    worker_running = true;
    worker = std::thread(&ChunkSystem::workerLoop, this);
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
                        { allReady = false; break; }
        }
        if (allReady) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::vector<ChunkCoord> ChunkSystem::getAndClearReadyChunks() {
    std::lock_guard<std::mutex> lk(readyMutex);
    std::vector<ChunkCoord> out = std::move(readyChunks);
    readyChunks.clear();
    return out;
}

void ChunkSystem::shutdown() {
    worker_running = false;
    genQueueCV.notify_all();
    if (worker.joinable()) worker.join();
}

// ------------------------------------------------------------------ private

ChunkCoord ChunkSystem::worldToChunk(const glm::vec3& pos) {
    return { (int)std::floor(pos.x / CHUNK_SIZE),
             (int)std::floor(pos.y / CHUNK_SIZE),
             (int)std::floor(pos.z / CHUNK_SIZE) };
}

void ChunkSystem::enqueueNeededChunks(const ChunkCoord& center) {
    for (int dx = -renderDistance; dx <= renderDistance; ++dx) {
        for (int dz = -renderDistance; dz <= renderDistance; ++dz) {
            ChunkCoord c{ center.x + dx, 0, center.z + dz };
            std::lock_guard<std::mutex> lk(runtimeMutex);
            auto& rt = runtime[c];
            if (rt.state == ChunkState::Unloaded) {
                rt.state = ChunkState::Queued;
                std::lock_guard<std::mutex> qlk(genQueueMutex);
                genQueue.push(c);
                genQueueCV.notify_one();
            }
        }
    }
}

void ChunkSystem::enqueueIfNeeded(const ChunkCoord& coord) {
    std::lock_guard<std::mutex> lk(runtimeMutex);
    auto& rt = runtime[coord];
    
    if (rt.state == ChunkState::Unloaded) {
        rt.state = ChunkState::Queued;
        printf("[ChunkSystem] Queued (%d, %d)\n", coord.x, coord.z);
        std::lock_guard<std::mutex> qlk(genQueueMutex);
        genQueue.push(coord);
        genQueueCV.notify_one();
    }
}

void ChunkSystem::workerLoop() {
    while (worker_running) {
        ChunkCoord coord;
        {
            std::unique_lock<std::mutex> lk(genQueueMutex);
            genQueueCV.wait(lk, [this]{
                return !genQueue.empty() || !worker_running;
            });
            if (!worker_running) break;
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
    auto chunk = generateTerrain(coord);

    // Write into world state
    {
        // WorldState needs its own mutex — add one, or use runtimeMutex
        // carefully. Here we add a worldMutex to Server and pass it in,
        // OR use the simpler approach: WorldState gets a mutex member.
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