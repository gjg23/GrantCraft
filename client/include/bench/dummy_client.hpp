#pragma once
// bench/dummy_client.hpp
// A headless game client that executes a pre-baked movement script in its
// own thread. Used by the load test to simulate real players.

#include "client_core/client.hpp"
#include "bench/metrics.hpp"

#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

// velocity + how long to hold it
struct MoveSegment {
    float vx        = 0.f;
    float vz        = 0.f;
    float durationS = 5.f;
};

struct DummyClientConfig {
    const char* host          = "localhost";
    uint16_t    port          = 5070;
    uint32_t    clientIdx     = 0;
    int         seed          = 0;
    int         renderRadius  = 4;
    float       spawnRadius   = 300.f;   // range for randomised start position
    float       moveSpeed     = 100.0f;   // world units / second
    float       testDurationS = 120.f;
    float       stationaryS   = 10.f;   // stand still at spawn before moving
                                         // (lets us measure initial-load time cleanly)
};

class DummyClient {
public:
    explicit DummyClient(DummyClientConfig cfg);
    ~DummyClient() { stop(); join(); }

    void start();
    void stop();
    void join();

    // ---- Lock-free metrics ----
    uint64_t chunksReceived()   const { return m_chunks.load(std::memory_order_relaxed); }
    uint64_t bytesReceived()    const { return m_bytes.load(std::memory_order_relaxed); }
    bool     isConnected()      const { return m_connected.load(std::memory_order_relaxed); }
    bool     isFullyLoaded()    const { return m_fullLoadNs.load(std::memory_order_relaxed) != 0; }

    ClientStats finalise(float testDurationS) const;

private:
    DummyClientConfig m_cfg;
    std::thread       m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_connected{ false };

    std::atomic<uint64_t> m_chunks{ 0 };
    std::atomic<uint64_t> m_bytes{ 0 };
    std::atomic<uint64_t> m_startNs{ 0 };
    std::atomic<uint64_t> m_firstChunkNs{ 0 };
    std::atomic<uint64_t> m_fullLoadNs{ 0 };

    std::vector<MoveSegment> m_script;
    glm::vec3                m_startPos{ 0.f };
    uint64_t                 m_expectedChunks = 0;

    void threadMain();

    static std::vector<MoveSegment> buildScript(int seed, float speed,
                                                 float totalS, float stationaryS);
    static glm::vec3 randomStartPos(int seed, float radius);
    static uint64_t  nowNs();
    static uint64_t  computeExpectedChunks(int radius);
};