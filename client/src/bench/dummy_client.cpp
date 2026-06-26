// bench/dummy_client.cpp

#include "bench/dummy_client.hpp"
#include "world/chunk.hpp"

#include <random>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <algorithm>

static constexpr float TICK_HZ      = 20.f;
static constexpr float TICK_S       = 1.f / TICK_HZ;
static constexpr float START_Y      = 64.f;
static constexpr int   MAX_RETRIES  = 5;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
uint64_t DummyClient::nowNs() {
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

uint64_t DummyClient::computeExpectedChunks(int radius) {
    // Mirror the server's interest system: c.y >= 0 only
    const int playerChunkY = static_cast<int>(START_Y) / CHUNK_SIZE;
    uint64_t count = 0;
    for (int dx = -radius; dx <= radius; ++dx)
    for (int dy = -radius; dy <= radius; ++dy)
    for (int dz = -radius; dz <= radius; ++dz)
        if (playerChunkY + dy >= 0) ++count;
    return count;
}

glm::vec3 DummyClient::randomStartPos(int seed, float radius) {
    std::mt19937 rng(static_cast<uint32_t>(seed + 0xBEEF));
    std::uniform_real_distribution<float> d(-radius, radius);
    return { d(rng), START_Y, d(rng) };
}

std::vector<MoveSegment> DummyClient::buildScript(int seed, float speed, float totalS, float stationaryS) {
    std::vector<MoveSegment> script;

    // Stand still first
    script.push_back({ 0.f, 0.f, stationaryS });

    // Random walk
    std::mt19937 rng(static_cast<uint32_t>(seed));
    std::uniform_real_distribution<float> angleDist(0.f, 2.f * 3.14159265f);
    std::uniform_real_distribution<float> durDist(3.f, 15.f);

    float remaining = totalS - stationaryS;
    while (remaining > 0.01f) {
        float angle = angleDist(rng);
        float dur   = std::min(durDist(rng), remaining);
        script.push_back({ std::cos(angle) * speed, std::sin(angle) * speed, dur });
        remaining -= dur;
    }

    return script;
}


// -----------------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------------
DummyClient::DummyClient(DummyClientConfig cfg) : m_cfg(cfg) {
    m_startPos       = randomStartPos(cfg.seed, cfg.spawnRadius);
    m_script         = buildScript(cfg.seed, cfg.moveSpeed,
                                   cfg.testDurationS, cfg.stationaryS);
    m_expectedChunks = computeExpectedChunks(cfg.renderRadius);
}

void DummyClient::start() {
    m_running = true;
    m_thread  = std::thread(&DummyClient::threadMain, this);
}

void DummyClient::stop()  { m_running = false; }
void DummyClient::join()  { if (m_thread.joinable()) m_thread.join(); }


// -----------------------------------------------------------------------------
// Thread main
// -----------------------------------------------------------------------------
void DummyClient::threadMain() {
    using Clock = std::chrono::steady_clock;

    char username[16];
    snprintf(username, sizeof(username), "bot%u", m_cfg.clientIdx);

    Client client;
    client.setRenderDistance(m_cfg.renderRadius);

    // Retry loop — server may still be warming up
    bool connected = false;
    for (int attempt = 1; attempt <= MAX_RETRIES && m_running; ++attempt) {
        if (client.connect(m_cfg.host, m_cfg.port, username, 2000)) {
            connected = true;
            break;
        }
        printf("[Bot %u] Connect attempt %d/%d failed, retrying...\n",
               m_cfg.clientIdx, attempt, MAX_RETRIES);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (!connected) {
        printf("[Bot %u] Giving up after %d attempts.\n",
               m_cfg.clientIdx, MAX_RETRIES);
        m_running = false;
        return;
    }

    m_startNs.store(nowNs(), std::memory_order_relaxed);
    m_connected.store(true, std::memory_order_relaxed);

    glm::vec3 pos      = m_startPos;
    size_t    segIdx   = 0;
    float     segTimer = 0.f;

    auto lastTick = Clock::now();

    while (m_running.load(std::memory_order_relaxed)) {
        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - lastTick).count();
        lastTick  = now;
        dt        = std::min(dt, 0.1f);   // guard against spurious long sleeps

        // Advance movement script
        if (segIdx < m_script.size()) {
            const auto& seg = m_script[segIdx];
            pos.x     += seg.vx * dt;
            pos.z     += seg.vz * dt;
            segTimer  += dt;
            if (segTimer >= seg.durationS) {
                segTimer = 0.f;
                ++segIdx;
                // Loop back to movement phase
                if (segIdx >= m_script.size()) segIdx = 1;
            }
        }

        // Network tick
        client.tick();

        // Process newly arrived chunks
        auto newChunks = client.drainNewChunks();
        if (!newChunks.empty()) {
            uint64_t total = m_chunks.fetch_add(
                (uint64_t)newChunks.size(), std::memory_order_relaxed)
                + (uint64_t)newChunks.size();

            if (m_firstChunkNs.load(std::memory_order_relaxed) == 0)
                m_firstChunkNs.store(nowNs(), std::memory_order_relaxed);

            if (m_fullLoadNs.load(std::memory_order_relaxed) == 0
                && total >= m_expectedChunks)
                m_fullLoadNs.store(nowNs(), std::memory_order_relaxed);
        }

        // Mirror byte counter (written + read on this thread; atomic so
        // LoadTest can safely snapshot it from the main thread)
        m_bytes.store(client.getTotalBytesReceived(), std::memory_order_relaxed);

        // Send our position to the server and update chunk interest
        client.sendInput(pos.x, pos.y, pos.z, 0.f, 0.f);
        client.updatePlayerChunk(pos.x, pos.y, pos.z);

        std::this_thread::sleep_for(
            std::chrono::duration<long long, std::micro>(
                static_cast<long long>(TICK_S * 1'000'000.f)));
    }

    client.disconnect();
    m_connected.store(false, std::memory_order_relaxed);
}


// -----------------------------------------------------------------------------
// Finalise
// -----------------------------------------------------------------------------
ClientStats DummyClient::finalise(float testDurationS) const {
    ClientStats s;
    s.clientIdx      = m_cfg.clientIdx;
    s.chunksReceived = m_chunks.load();
    s.bytesReceived  = m_bytes.load();

    uint64_t startNs = m_startNs.load();
    uint64_t firstNs = m_firstChunkNs.load();
    uint64_t fullNs  = m_fullLoadNs.load();

    s.timeToFirstChunkMs = (firstNs > 0 && startNs > 0)
        ? (float)(firstNs - startNs) / 1e6f : -1.f;

    s.timeToFullLoadS = (fullNs > 0 && startNs > 0)
        ? (float)(fullNs - startNs) / 1e9f : -1.f;

    if (testDurationS > 0.f) {
        s.avgChunksPerSec = (float)s.chunksReceived / testDurationS;
        s.avgBwKBps       = (float)s.bytesReceived  / testDurationS / 1024.f;
    }

    return s;
}