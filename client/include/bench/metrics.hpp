#pragma once
// bench/metrics.hpp

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <algorithm>
#include <numeric>

// Time series for graphing
struct TimeSample {
    float    timeS              = 0.f;
    uint32_t playerCount        = 0;
    float    tickRateHz         = 0.f;
    float    tickBudgetPct      = 0.f;
    float    workerQueueDepth   = 0.f;
    float    bytesSentKBps      = 0.f;
    float    totalChunksPerSec  = 0.f;
    float    totalBlocksPerSecM = 0.f;
    float    avgPingMs          = 0.f;
};

// Dummy client stats per client
struct ClientStats {
    uint32_t clientIdx           = 0;
    uint64_t chunksReceived      = 0;
    uint64_t bytesReceived       = 0;
    float    timeToFirstChunkMs  = -1.f;
    float    timeToFullLoadS     = -1.f;
    float    avgChunksPerSec     = 0.f;
    float    avgBwKBps           = 0.f;
};

// ---- Full load-test report ----
struct LoadTestReport {
    // Config
    uint32_t playerCount    = 0;
    uint32_t renderRadius   = 0;
    float    testDurationS  = 0.f;

    // Server health
    float avgTickRateHz          = 0.f;
    float minTickRateHz          = 0.f;
    float maxTickRateHz          = 0.f;
    float avgTickBudgetPct       = 0.f;
    float maxTickBudgetPct       = 0.f;
    float avgWorkerQueueDepth    = 0.f;

    // Bandwidth
    float avgBytesSentKBps       = 0.f;
    float peakBytesSentKBps      = 0.f;

    // Throughput
    float avgTotalChunksPerSec   = 0.f;
    float avgTotalBlocksPerSecM  = 0.f;
    float totalDataTransferredMB = 0.f;

    // Latency
    float avgTimeToFirstChunkMs  = 0.f;
    float avgTimeToFullLoadS     = 0.f;
    float avgPingMs              = 0.f;
    int   clientsFullyLoaded     = 0;

    std::vector<ClientStats> perClient;
    std::vector<TimeSample>  timeSeries;
};

// ---- Generation-only benchmark result ----
struct GenBenchResult {
    std::string backend;
    uint32_t    chunksGenerated = 0;
    uint64_t    totalBlocks     = 0;
    float       totalTimeS      = 0.f;
    float       chunksPerSec    = 0.f;
    float       blocksPerSecM   = 0.f;
};

// ---- Stat helpers ----
template<typename T>
inline float statsAvg(const std::vector<T>& v) {
    if (v.empty()) return 0.f;
    return (float)std::accumulate(v.begin(), v.end(), T{}) / (float)v.size();
}
template<typename T>
inline T statsMin(const std::vector<T>& v) {
    return v.empty() ? T{} : *std::min_element(v.begin(), v.end());
}
template<typename T>
inline T statsMax(const std::vector<T>& v) {
    return v.empty() ? T{} : *std::max_element(v.begin(), v.end());
}

// ---- CSV writers ----
inline void writeLoadTestCSV(const std::string& path, const LoadTestReport& r) {
    std::ofstream f(path);
    if (!f) { fprintf(stderr, "[Metrics] Cannot write CSV: %s\n", path.c_str()); return; }
    f << "time_s,player_count,tick_rate_hz,tick_budget_pct,"
         "worker_queue_depth,bytes_sent_kbps,"
         "total_chunks_per_sec,total_blocks_per_sec_M,avg_ping_ms\n";
    for (const auto& s : r.timeSeries)
        f << s.timeS              << ','
          << s.playerCount        << ','
          << s.tickRateHz         << ','
          << s.tickBudgetPct      << ','
          << s.workerQueueDepth   << ','
          << s.bytesSentKBps      << ','
          << s.totalChunksPerSec  << ','
          << s.totalBlocksPerSecM << ','
          << s.avgPingMs          << '\n';
    printf("[Metrics] Wrote %zu samples → %s\n", r.timeSeries.size(), path.c_str());
}

inline void writeGenBenchCSV(const std::string& path,
                              const std::vector<GenBenchResult>& results) {
    std::ofstream f(path);
    if (!f) { fprintf(stderr, "[Metrics] Cannot write CSV: %s\n", path.c_str()); return; }
    f << "backend,chunks_generated,total_blocks,time_s,chunks_per_sec,blocks_per_sec_M\n";
    for (const auto& r : results)
        f << r.backend         << ','
          << r.chunksGenerated << ','
          << r.totalBlocks     << ','
          << r.totalTimeS      << ','
          << r.chunksPerSec    << ','
          << r.blocksPerSecM   << '\n';
    printf("[Metrics] Wrote gen bench CSV → %s\n", path.c_str());
}

// Defined in metrics.cpp
void printGenBenchReport(const std::vector<GenBenchResult>& results);
void printLoadTestReport(const LoadTestReport& report);