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

enum class GenBackend {
    CPU,
    GPU,  // boilerplate for now - currently no gpu support
};

struct ChunkRuntime {
    ChunkState state = ChunkState::Unloaded;
};

class ChunkSystem {
public:
    ChunkSystem(WorldState& world, GenBackend backend = GenBackend::CPU);
    void init();

    void update(const std::vector<PlayerSnapshot>& snapshots);

    void waitForSpawnChunks(int spawnRadius = 2);

    std::vector<ChunkCoord> getAndClearReadyChunks();

    void setRenderDistance(int r) { renderDistance = r; }

    void enqueueIfNeeded(const ChunkCoord& coord, const std::vector<ChunkCoord>& centers);      // single

    void shutdown();
private:
    // World data to broadcast
    WorldState& world;
    // GPU or CPU device
    GenBackend  backend;

    // Chunk pipeline map
    std::unordered_map<ChunkCoord, ChunkRuntime, ChunkCoordHash> runtime;
    std::mutex runtimeMutex;


    // ============================
    // Gen queue
    // ============================
    // Priority queue sorted nearest-first
    struct PriCoord {
        ChunkCoord coord;
        int        distSq;  // XZ distance squared from nearest player
        bool operator>(const PriCoord& o) const { return distSq > o.distSq; }
    };
    std::priority_queue<PriCoord,
                        std::vector<PriCoord>,
                        std::greater<PriCoord>> genQueue;
    std::mutex              genQueueMutex;  // mutex
    std::condition_variable genQueueCV;     // signal

    // ============================
    // Ready list
    // ============================
    std::vector<ChunkCoord> readyChunks;
    std::mutex              readyMutex;

    // ============================
    // Worker thread pool
    // ============================
    std::vector<std::thread> workers;
    std::atomic<bool> worker_running{false};

    int renderDistance = 8; // TODO - make setting.hpp namespace thing

    // Most recent player chunk centers — used for priority scoring
    int closestDistSq(const ChunkCoord& coord, const std::vector<ChunkCoord>& centers);

    // chunk gen pipeline
    void enqueueNeededChunks(const ChunkCoord& center); // multiple
    void workerLoop();
    void generateChunk(const ChunkCoord& coord);

    // Helper to get world cord to chunk coord
    ChunkCoord worldToChunk(const glm::vec3& pos);
};