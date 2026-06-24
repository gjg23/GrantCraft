// bench/bench_mode.cpp

#include "bench/bench_mode.hpp"
#include "client_core/client.hpp"
#include "settings/settings.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>
#include <numeric>
#include <vector>
#include <cmath>

// ---- internal metrics ----------------------------------------
struct BenchMetrics {
    // Timing
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point firstChunkTime;
    bool firstChunkRecorded = false;

    // Chunk counts
    uint64_t totalBlocksReceived = 0;
    uint64_t totalChunksReceived = 0;

    // Per-second samples for min/max/avg
    struct Sample {
        float blocksPerSec;
        float chunksPerSec;
        float pingMs;
    };
    std::vector<Sample> samples;

    // Per-tick ping approximation (round trip of debug snapshot)
    std::vector<float> pings;
    float lastPingMs = 0.f;

    // Bandwidth
    uint64_t totalBytesReceived = 0;
    std::vector<float> bwSamplesKBps;

    // Server metrics from debug snapshots
    std::vector<uint32_t> tickRateSamples;
    std::vector<float>    tickBudgetSamples;
    std::vector<uint32_t> workerQueueSamples;
};

static void printSummary(const BenchMetrics& m, int targetChunks) {
    using namespace std::chrono;

    auto now      = steady_clock::now();
    float totalS  = duration<float>(now - m.startTime).count();
    float activeS = m.firstChunkRecorded
                  ? duration<float>(now - m.firstChunkTime).count()
                  : totalS;

    float avgBlocksPerSec = activeS > 0.f
        ? static_cast<float>(m.totalBlocksReceived) / activeS : 0.f;
    float avgChunksPerSec = activeS > 0.f
        ? static_cast<float>(m.totalChunksReceived) / activeS : 0.f;

    auto minmax_avg = [](const std::vector<float>& v) -> std::tuple<float,float,float> {
        if (v.empty()) return {0,0,0};
        float mn = *std::min_element(v.begin(), v.end());
        float mx = *std::max_element(v.begin(), v.end());
        float av = std::accumulate(v.begin(), v.end(), 0.f) / v.size();
        return {mn, mx, av};
    };
    auto minmax_avg_u = [](const std::vector<uint32_t>& v) -> std::tuple<uint32_t,uint32_t,float> {
        if (v.empty()) return {0,0,0};
        uint32_t mn = *std::min_element(v.begin(), v.end());
        uint32_t mx = *std::max_element(v.begin(), v.end());
        float    av = std::accumulate(v.begin(), v.end(), 0u) / (float)v.size();
        return {mn, mx, av};
    };

    auto [pingMin, pingMax, pingAvg]     = minmax_avg(m.pings);
    auto [bwMin,   bwMax,   bwAvg]       = minmax_avg(m.bwSamplesKBps);
    auto [trMin,   trMax,   trAvg]       = minmax_avg_u(m.tickRateSamples);
    auto [tbMin,   tbMax,   tbAvg]       = minmax_avg(m.tickBudgetSamples);
    auto [wqMin,   wqMax,   wqAvg]       = minmax_avg_u(m.workerQueueSamples);

    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║            BENCH MODE — FINAL SUMMARY           ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Target chunks          : %-6d                 ║\n", targetChunks);
    printf("║  Chunks received        : %-6llu                ║\n", (unsigned long long)m.totalChunksReceived);
    printf("║  Blocks received        : %-12llu           ║\n",   (unsigned long long)m.totalBlocksReceived);
    printf("║  Total elapsed          : %-6.2f s              ║\n", totalS);
    printf("║  Active recv time       : %-6.2f s              ║\n", activeS);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Avg blocks/s           : %-10.0f           ║\n", avgBlocksPerSec);
    printf("║  Avg chunks/s           : %-8.2f             ║\n", avgChunksPerSec);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Ping (ms)  min/avg/max : %.1f / %.1f / %.1f\n",   pingMin, pingAvg, pingMax);
    printf("║  Bandwidth  min/avg/max : %.1f / %.1f / %.1f KB/s\n", bwMin, bwAvg, bwMax);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Server tick rate       : %u / %.1f / %u Hz\n",   trMin, trAvg, trMax);
    printf("║  Tick budget used %%     : %.1f / %.1f / %.1f\n", tbMin, tbAvg, tbMax);
    printf("║  Worker queue depth     : %u / %.1f / %u\n",      wqMin, wqAvg, wqMax);
    printf("╚══════════════════════════════════════════════════╝\n");
}

void runBenchMode(const char* host, uint16_t port,
                  const char* username, int radiusOverride)
{
    int r = (radiusOverride > 0) ? radiusOverride : ClientCfg::RENDER_DISTANCE;
    // cubic chunk count for the given radius
    int targetChunks = (2*r+1) * (2*r+1) * (2*r+1);

    printf("[Bench] Connecting to %s:%u ...\n", host, port);
    printf("[Bench] Target: %d chunks (radius %d)\n", targetChunks, r);

    Client client;
    if (!client.connect(host, port, username)) {
        fprintf(stderr, "[Bench] Connection failed.\n");
        return;
    }

    BenchMetrics m;
    m.startTime = std::chrono::steady_clock::now();

    using Clock = std::chrono::steady_clock;
    auto lastSampleTime   = Clock::now();
    auto lastDebugQuery   = Clock::now();
    auto lastDebugReply   = Clock::now();

    uint64_t chunksSinceLastSample = 0;
    uint64_t bytesSinceLastSample  = 0;
    constexpr float SAMPLE_INTERVAL_S = 1.0f;
    constexpr float DEBUG_QUERY_INTERVAL_S = 0.25f;

    while (m.totalChunksReceived < static_cast<uint64_t>(targetChunks)) {
        client.tick();

        // Count new chunks
        auto newChunks = client.getAndClearNewChunks();
        if (!newChunks.empty() && !m.firstChunkRecorded) {
            m.firstChunkTime     = Clock::now();
            m.firstChunkRecorded = true;
        }
        for (auto& coord : newChunks) {
            (void)coord;
            m.totalChunksReceived++;
            m.totalBlocksReceived += CHUNK_VOLUME;
            chunksSinceLastSample++;
            bytesSinceLastSample  += sizeof(PKT_S_ChunkData);
        }

        // Handle incoming debug snapshots
        if (client.hasPendingDebugSnapshot()) {
            auto snap = client.popDebugSnapshot();
            float rtt = std::chrono::duration<float, std::milli>(
                Clock::now() - lastDebugQuery).count();
            m.pings.push_back(rtt);
            m.lastPingMs = rtt;
            m.tickRateSamples.push_back(snap.tickRateHz);
            m.tickBudgetSamples.push_back(snap.tickBudgetUsedPct);
            m.workerQueueSamples.push_back(snap.workerQueueDepth);
            bytesSinceLastSample += snap.bytesRecvThisTick;
        }

        // Send periodic debug queries
        float sinceQuery = std::chrono::duration<float>(
            Clock::now() - lastDebugQuery).count();
        if (sinceQuery >= DEBUG_QUERY_INTERVAL_S) {
            client.sendDebugQuery();
            lastDebugQuery = Clock::now();
        }

        // Per-second samples
        float sinceLastSample = std::chrono::duration<float>(
            Clock::now() - lastSampleTime).count();
        if (sinceLastSample >= SAMPLE_INTERVAL_S) {
            float bps = static_cast<float>(chunksSinceLastSample * CHUNK_VOLUME)
                      / sinceLastSample;
            float cps = static_cast<float>(chunksSinceLastSample) / sinceLastSample;
            float bwKBps = static_cast<float>(bytesSinceLastSample)
                         / sinceLastSample / 1024.f;

            m.samples.push_back({ bps, cps, m.lastPingMs });
            m.bwSamplesKBps.push_back(bwKBps);

            printf("[Bench] %llu/%d chunks  |  %.0f blocks/s  |  %.1f KB/s  |  ping %.1fms\n",
                   (unsigned long long)m.totalChunksReceived,
                   targetChunks, bps, bwKBps, m.lastPingMs);

            chunksSinceLastSample = 0;
            bytesSinceLastSample  = 0;
            lastSampleTime        = Clock::now();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    client.disconnect();
    printSummary(m, targetChunks);
}