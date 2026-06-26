#include "bench/load_test.hpp"
#include "bench/dummy_client.hpp"
#include "world/chunk.hpp"

#include <cstdio>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <numeric>
#include <algorithm>

// Internal metrics client for debug queries
class MetricsClient {
public:
    bool init(const char* host, uint16_t port) {
        if (!m_client.connect(host, port, "bench_metrics", 3000)) {
            fprintf(stderr, "[LoadTest] Metrics client failed to connect.\n");
            return false;
        }
        m_lastQueryAt = Clock::now();
        return true;
    }

    // Call once per main-loop iteration
    void tick() {
        m_client.tick();

        if (m_client.hasPendingDebugSnapshot()) {
            auto snap = m_client.popDebugSnapshot();
            auto now  = Clock::now();

            float rttMs = std::chrono::duration<float, std::milli>(
                now - m_lastQueryAt).count();

            // Time-based KB/s — independent of tick rate
            float kbps = 0.f;
            if (m_hasLastSnapTime) {
                float dtS = std::chrono::duration<float>(now - m_lastSnapTime).count();
                if (dtS > 0.f)
                    kbps = (float)snap.bytesSentThisTick / dtS / 1024.f;
            }
            m_lastSnapTime    = now;
            m_hasLastSnapTime = true;

            tickRateHz.push_back((float)snap.tickRateHz);
            tickBudgetPct.push_back(snap.tickBudgetUsedPct);
            workerQueueDepth.push_back((float)snap.workerQueueDepth);
            pingMs.push_back(rttMs);
            bytesSentKBps.push_back(kbps);
        }

        float sinceQuery = std::chrono::duration<float>(Clock::now() - m_lastQueryAt).count();
        if (sinceQuery >= 0.5f) {
            m_client.sendDebugQuery();
            m_lastQueryAt = Clock::now();
        }
    }
    void disconnect() { m_client.disconnect(); }

    // Sampled vectors — consumed by LoadTest to build the report
    std::vector<float> tickRateHz;
    std::vector<float> tickBudgetPct;
    std::vector<float> workerQueueDepth;
    std::vector<float> bytesSentKBps;
    std::vector<float> pingMs;

private:
    using Clock = std::chrono::steady_clock;
    Client            m_client;
    Clock::time_point m_lastQueryAt;
    Clock::time_point m_lastSnapTime;
    bool              m_hasLastSnapTime = false;
};


// run testing
LoadTestReport runLoadTest(const LoadTestConfig& cfg) {
    using Clock = std::chrono::steady_clock;

    printf("\n[LoadTest] %u players  radius=%d  %.0fs  port=%u\n",
           cfg.playerCount, cfg.renderRadius,
           cfg.testDurationS, cfg.port);

    // --- Start fresh local server
    LocalServer localServer;
    printf("[LoadTest] Starting server...\n");
    if (!localServer.start(cfg.port, cfg.serverMode)) {
        fprintf(stderr, "[LoadTest] ERROR: Could not start local server.\n");
        return {};
    }
    // Wait for spawn chunks to generate before clients pile in
    printf("[LoadTest] Waiting for spawn chunks (3s)...\n");
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Connect metrics client first
    MetricsClient metrics;
    if (!metrics.init("localhost", cfg.port)) {
        localServer.stop();
        return {};
    }

    // Create dummy clients
    std::vector<std::unique_ptr<DummyClient>> clients;
    clients.reserve(cfg.playerCount);
    for (uint32_t i = 0; i < cfg.playerCount; ++i) {
        DummyClientConfig dc;
        dc.host          = "localhost";
        dc.port          = cfg.port;
        dc.clientIdx     = i;
        dc.seed          = (int)i * 31 + 7;
        dc.renderRadius  = cfg.renderRadius;
        dc.spawnRadius   = cfg.spawnRadius;
        dc.moveSpeed     = cfg.moveSpeed;
        dc.testDurationS = cfg.testDurationS;
        dc.stationaryS   = cfg.stationaryS;
        clients.push_back(std::make_unique<DummyClient>(dc));
    }

    // Stagger connections by 150ms to avoid ENet connection storm
    printf("[LoadTest] Connecting %u clients (staggered)...\n", cfg.playerCount);
    for (auto& c : clients) {
        c->start();
        // Keep metrics connection alive during stagger — ENet times out ~5s without service
        for (int i = 0; i < 15; ++i) {
            metrics.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    printf("[LoadTest] All clients running. Test started.\n\n");

    // Sampling
    auto testStart  = Clock::now();
    auto lastSample = testStart;

    std::vector<uint64_t> prevChunks(cfg.playerCount, 0);
    std::vector<float>    chunkRateSamples;
    std::vector<TimeSample> timeSeries;

    while (true) {
        float elapsedS = std::chrono::duration<float>(
            Clock::now() - testStart).count();
        if (elapsedS >= cfg.testDurationS) break;

        metrics.tick();

        float sinceSample = std::chrono::duration<float>(
            Clock::now() - lastSample).count();

        if (sinceSample >= 1.0f) {
            lastSample = Clock::now();

            // Count newly received chunks across all clients since last sample
            uint64_t newChunks = 0;
            for (uint32_t i = 0; i < cfg.playerCount; ++i) {
                uint64_t cur  = clients[i]->chunksReceived();
                newChunks    += cur - prevChunks[i];
                prevChunks[i] = cur;
            }

            float cps     = (float)newChunks / sinceSample;
            float mblocksS = cps * (float)CHUNK_VOLUME / 1e6f;
            chunkRateSamples.push_back(cps);

            // Take latest server metrics
            float tickHz  = metrics.tickRateHz.empty()    ? 0.f : metrics.tickRateHz.back();
            float budget  = metrics.tickBudgetPct.empty() ? 0.f : metrics.tickBudgetPct.back();
            float wq      = metrics.workerQueueDepth.empty() ? 0.f : metrics.workerQueueDepth.back();
            float bwKBps  = metrics.bytesSentKBps.empty() ? 0.f : metrics.bytesSentKBps.back();
            float ping    = metrics.pingMs.empty()         ? 0.f : metrics.pingMs.back();

            timeSeries.push_back({ elapsedS, cfg.playerCount,
                tickHz, budget, wq, bwKBps, cps, mblocksS, ping });

            printf("[LoadTest] %5.0fs | tick %4.0fHz (%4.0f%%) | "
                   "chunks/s %6.0f | Mblk/s %5.3f | BW %6.0f KB/s | ping %4.0fms\n",
                   elapsedS, tickHz, budget, cps, mblocksS, bwKBps, ping);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Stop test
    printf("\n[LoadTest] Stopping clients...\n");
    for (auto& c : clients) c->stop();
    for (auto& c : clients) c->join();
    metrics.disconnect();
    localServer.stop();

    // Print reports
    LoadTestReport report;
    report.playerCount   = cfg.playerCount;
    report.renderRadius  = (uint32_t)cfg.renderRadius;
    report.testDurationS = cfg.testDurationS;
    report.timeSeries    = std::move(timeSeries);

    report.avgTickRateHz       = statsAvg(metrics.tickRateHz);
    report.minTickRateHz       = statsMin(metrics.tickRateHz);
    report.maxTickRateHz       = statsMax(metrics.tickRateHz);
    report.avgTickBudgetPct    = statsAvg(metrics.tickBudgetPct);
    report.maxTickBudgetPct    = statsMax(metrics.tickBudgetPct);
    report.avgWorkerQueueDepth = statsAvg(metrics.workerQueueDepth);
    report.avgBytesSentKBps    = statsAvg(metrics.bytesSentKBps);
    report.peakBytesSentKBps   = statsMax(metrics.bytesSentKBps);

    report.avgTotalChunksPerSec  = statsAvg(chunkRateSamples);
    report.avgTotalBlocksPerSecM = report.avgTotalChunksPerSec * (float)CHUNK_VOLUME / 1e6f;

    uint64_t totalBytes = 0;
    for (const auto& c : clients) totalBytes += c->bytesReceived();
    report.totalDataTransferredMB = (float)totalBytes / (1024.f * 1024.f);

    std::vector<float> firstChunkMs, fullLoadS;
    for (uint32_t i = 0; i < cfg.playerCount; ++i) {
        ClientStats cs = clients[i]->finalise(cfg.testDurationS);
        report.perClient.push_back(cs);
        if (cs.timeToFirstChunkMs >= 0.f) firstChunkMs.push_back(cs.timeToFirstChunkMs);
        if (cs.timeToFullLoadS    >= 0.f) {
            fullLoadS.push_back(cs.timeToFullLoadS);
            ++report.clientsFullyLoaded;
        }
    }

    report.avgTimeToFirstChunkMs = statsAvg(firstChunkMs);
    report.avgTimeToFullLoadS    = statsAvg(fullLoadS);
    report.avgPingMs             = statsAvg(metrics.pingMs);

    printLoadTestReport(report);
    if (!cfg.csvPath.empty()) writeLoadTestCSV(cfg.csvPath, report);

    return report;
}