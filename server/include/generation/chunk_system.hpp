#pragma once
// ==========================================================
// chunk_system.hpp
// manages chunk lifecycle
// ==========================================================

#include <unordered_map>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <glm/glm.hpp>

#include "world/world_state.hpp"
#include "world/chunk.hpp"

// View of current player info
struct PlayerSnapshot {
    uint32_t  id;
    glm::vec3 pos;
};

// Chunk state machine
enum class ChunkState {
    Unloaded,
    Queued,
    Generating,
    Ready,
};

struct ChunkRuntime {
    ChunkState state = ChunkState::Unloaded;
};

class ChunkSystem {
public:
    ChunkSystem(WorldState& world);
    void init();

    void update(const std::vector<PlayerSnapshot>& snapshots);

    void waitForSpawnChunks(int spawnRadius = 2);

    std::vector<ChunkCoord> getAndClearReadyChunks();

    void setRenderDistance(int r) { renderDistance = r; }

    void enqueueIfNeeded(const ChunkCoord& coord);      // single

    void shutdown();
private:
    // World data to broadcast
    WorldState& world;

    // Chunk pipeline map
    std::unordered_map<ChunkCoord, ChunkRuntime, ChunkCoordHash> runtime;
    std::mutex runtimeMutex;

    // Gen queue
    std::queue<ChunkCoord>  genQueue;
    std::mutex              genQueueMutex;
    std::condition_variable genQueueCV;

    // Ready list
    std::vector<ChunkCoord> readyChunks;
    std::mutex              readyMutex;

    // Worker thread
    std::thread       worker;
    std::atomic<bool> worker_running{false};

    int renderDistance = 8;

    // chunk gen pipeline
    void enqueueNeededChunks(const ChunkCoord& center); // multiple
    void workerLoop();
    void generateChunk(const ChunkCoord& coord);

    // Helper to get world cord to chunk coord
    ChunkCoord worldToChunk(const glm::vec3& pos);
};