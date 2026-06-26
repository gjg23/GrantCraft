// bench/metrics.cpp

#include "bench/metrics.hpp"
#include <cstdio>

void printGenBenchReport(const std::vector<GenBenchResult>& results) {
    printf("\n");
    printf("╔═════════════════════════════════════════════════════════════╗\n");
    printf("║              GENERATION BENCHMARK — RESULTS                 ║\n");
    printf("╠══════════════╦══════════╦══════════╦════════════╦═══════════╣\n");
    printf("║ Backend      ║  Chunks  ║  Time(s) ║  Chunks/s  ║ Mblk/s    ║\n");
    printf("╠══════════════╬══════════╬══════════╬════════════╬═══════════╣\n");
    for (const auto& r : results)
        printf("║ %-12s ║ %8u ║ %8.3f ║ %10.1f ║ %7.2f  ║\n",
               r.backend.c_str(), r.chunksGenerated,
               r.totalTimeS, r.chunksPerSec, r.blocksPerSecM);
    printf("╚══════════════╩══════════╩══════════╩════════════╩══════════╝\n");
}

void printLoadTestReport(const LoadTestReport& r) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║              LOAD TEST — FINAL REPORT                        ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Players: %-3u   Radius: %-3u   Duration: %-5.0fs            ║\n",
           r.playerCount, r.renderRadius, r.testDurationS);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  SERVER HEALTH                                               ║\n");
    printf("║  Tick rate Hz     avg / min / max : %6.1f / %6.1f / %6.1f ║\n",
           r.avgTickRateHz, r.minTickRateHz, r.maxTickRateHz);
    printf("║  Tick budget %%   avg / max        : %6.1f / %6.1f         ║\n",
           r.avgTickBudgetPct, r.maxTickBudgetPct);
    printf("║  Worker queue depth avg           : %6.1f                  ║\n",
           r.avgWorkerQueueDepth);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  BANDWIDTH                                                   ║\n");
    printf("║  Avg  KB/s sent                   : %8.1f                ║\n",
           r.avgBytesSentKBps);
    printf("║  Peak KB/s sent                   : %8.1f                ║\n",
           r.peakBytesSentKBps);
    printf("║  Total data transferred           : %8.2f MB             ║\n",
           r.totalDataTransferredMB);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  THROUGHPUT                                                  ║\n");
    printf("║  Avg total chunks/s               : %8.1f                ║\n",
           r.avgTotalChunksPerSec);
    printf("║  Avg total Mblocks/s              : %8.3f                ║\n",
           r.avgTotalBlocksPerSecM);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  LATENCY                                                     ║\n");
    printf("║  Avg time to first chunk          : %8.1f ms             ║\n",
           r.avgTimeToFirstChunkMs);
    printf("║  Avg time to full initial load    : %8.2f s              ║\n",
           r.avgTimeToFullLoadS);
    printf("║  Avg ping (RTT)                   : %8.1f ms             ║\n",
           r.avgPingMs);
    printf("║  Clients that fully loaded        : %d / %d                  ║\n",
           r.clientsFullyLoaded, (int)r.playerCount);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  PER-CLIENT BREAKDOWN                                        ║\n");
    printf("║  %-4s  %-8s  %-12s  %-12s  %-10s ║\n",
           "Idx", "Chunks", "1stChunk(ms)", "FullLoad(s)", "BW(KB/s)");
    printf("║  ──────────────────────────────────────────────────────      ║\n");
    for (const auto& c : r.perClient) {
        char fc[12], fl[12];
        if (c.timeToFirstChunkMs < 0) snprintf(fc, sizeof(fc), "N/A");
        else snprintf(fc, sizeof(fc), "%.1f", c.timeToFirstChunkMs);
        if (c.timeToFullLoadS < 0) snprintf(fl, sizeof(fl), "N/A");
        else snprintf(fl, sizeof(fl), "%.2f", c.timeToFullLoadS);
        printf("║  %-4u  %-8llu  %-12s  %-12s  %-10.1f ║\n",
               c.clientIdx, (unsigned long long)c.chunksReceived,
               fc, fl, c.avgBwKBps);
    }
    printf("╚══════════════════════════════════════════════════════════════╝\n");
}