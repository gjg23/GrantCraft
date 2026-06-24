// ================================================================
// debug_mode.cpp
// htop-style live terminal dashboard.
// Uses ANSI escape codes — works on Linux/macOS terminal,
// Windows Terminal with VT enabled.
// ================================================================

#include "bench/debug_mode.hpp"
#include "client_core/client.hpp"
#include "net/debug_packets.hpp"
#include "settings/settings.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include <numeric>
#include <algorithm>
#include <csignal>
#include <atomic>

// ---- terminal helpers ----
#define ANSI_CLEAR       "\033[2J\033[H"
#define ANSI_BOLD        "\033[1m"
#define ANSI_RESET       "\033[0m"
#define ANSI_GREEN       "\033[32m"
#define ANSI_YELLOW      "\033[33m"
#define ANSI_RED         "\033[31m"
#define ANSI_CYAN        "\033[36m"
#define ANSI_WHITE       "\033[37m"
#define ANSI_DIM         "\033[2m"

static std::atomic<bool> g_running{ true };

static void sigHandler(int) { g_running = false; }

// ---- bar graph helper ----------------------------------------
// Renders a fixed-width bar
static void printBar(float fraction, int width, const char* color) {
    fraction = std::max(0.f, std::min(1.f, fraction));
    int filled = static_cast<int>(fraction * width);
    printf("%s[", color);
    for (int i = 0; i < width; ++i)
        printf("%s", i < filled ? "█" : "░");
    printf("]" ANSI_RESET " %3.0f%%", fraction * 100.f);
}

static void renderDashboard(const DashState& s) {
    // Move cursor to top-left without clearing (flicker-free)
    printf("\033[H");

    const auto& snap = s.lastSnap;
    const auto& reg  = snap.registry;

    printf(ANSI_BOLD "╔══════════════════════════════════════════════════════════════╗\n");
    printf("║        CHUNK PIPELINE DEBUGGER  (Ctrl+C to quit)             ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n" ANSI_RESET);

    // ---- elapsed / connection ----
    printf(ANSI_BOLD "  Elapsed: " ANSI_RESET "%-8.1fs    ", s.elapsedS);
    if (s.hasSnap)  printf(ANSI_GREEN "● Connected" ANSI_RESET "    ");
    else            printf(ANSI_YELLOW "○ Waiting for snapshot..." ANSI_RESET "    ");
    printf("\n\n");

    // ---- ChunkRegistry breakdown ----
    printf(ANSI_BOLD "  CHUNK REGISTRY  (total tracked: %u)\n" ANSI_RESET,
        s.hasSnap ? reg.totalTracked : 0);
    printf("  ─────────────────────────────────────────────────────────\n");

    uint32_t total = s.hasSnap ? std::max(1u, reg.totalTracked) : 1u;

    auto printRegRow = [&](const char* label, uint32_t count,
                           const char* color, const char* hint) {
        printf("  %-14s " ANSI_BOLD "%5u" ANSI_RESET "  ", label, count);
        printBar(static_cast<float>(count) / total, 30, color);
        printf("  " ANSI_DIM "%s" ANSI_RESET "\n", hint);
    };

    if (s.hasSnap) {
        printRegRow("Requested",  reg.requested,  ANSI_YELLOW,
                    "queued for worker pool");
        printRegRow("Generating", reg.generating, ANSI_CYAN,
                    "in worker pool / GPU");
        printRegRow("WorldReady", reg.worldReady, ANSI_GREEN,
                    "built, pending send");
        printRegRow("Sent",       reg.sent,       ANSI_WHITE,
                    "fully delivered");
        printf("\n");
        printf("  Pending recipient slots : " ANSI_BOLD "%u" ANSI_RESET
               "  (total unsent player×chunk pairs)\n",
               reg.totalPendingRecipients);
    } else {
        printf("  " ANSI_DIM "(no snapshot yet)" ANSI_RESET "\n");
    }

    // ---- Worker pool ----
    printf("\n");
    printf(ANSI_BOLD "  SERVER WORKER POOL\n" ANSI_RESET);
    printf("  ─────────────────────────────────────────────────────────\n");
    if (s.hasSnap) {
        printf("  Gen queue depth : " ANSI_BOLD "%u" ANSI_RESET
               "   GPU in-flight : " ANSI_BOLD "%u" ANSI_RESET "\n",
               snap.workerQueueDepth, snap.gpuJobsInFlight);
        printf("  Tick rate       : " ANSI_BOLD "%u Hz" ANSI_RESET
               "   Budget used : ",
               snap.tickRateHz);
        float budgetFrac = snap.tickBudgetUsedPct / 100.f;
        const char* budgetColor = budgetFrac < 0.7f ? ANSI_GREEN
                                : budgetFrac < 0.9f ? ANSI_YELLOW
                                : ANSI_RED;
        printBar(budgetFrac, 20, budgetColor);
        printf("\n");
    }

    // ---- Client-side throughput ----
    printf("\n");
    printf(ANSI_BOLD "  CLIENT THROUGHPUT  (1-second rolling)\n" ANSI_RESET);
    printf("  ─────────────────────────────────────────────────────────\n");
    printf("  Chunks received : " ANSI_BOLD "%llu" ANSI_RESET
           "   total blocks : " ANSI_BOLD "%llu" ANSI_RESET "\n",
           (unsigned long long)s.totalChunksRecv,
           (unsigned long long)s.totalBlocksRecv);
    printf("  Blocks/s  cur/avg/max : " ANSI_BOLD
           "%.0f / %.0f / %.0f" ANSI_RESET "\n",
           s.blocksPerSec.count ? s.blocksPerSec.buf[(s.blocksPerSec.pos-1+RollingStats::N)%RollingStats::N] : 0.f,
           s.blocksPerSec.avg(), s.blocksPerSec.max());
    printf("  Chunks/s  cur/avg/max : " ANSI_BOLD
           "%.1f / %.1f / %.1f" ANSI_RESET "\n",
           s.chunksPerSec.count ? s.chunksPerSec.buf[(s.chunksPerSec.pos-1+RollingStats::N)%RollingStats::N] : 0.f,
           s.chunksPerSec.avg(), s.chunksPerSec.max());
    printf("  Bandwidth KB/s cur/avg/max : " ANSI_BOLD
           "%.1f / %.1f / %.1f" ANSI_RESET "\n",
           s.bwKBps.count ? s.bwKBps.buf[(s.bwKBps.pos-1+RollingStats::N)%RollingStats::N] : 0.f,
           s.bwKBps.avg(), s.bwKBps.max());

    // ---- Connection ----
    printf("\n");
    printf(ANSI_BOLD "  CONNECTION\n" ANSI_RESET);
    printf("  ─────────────────────────────────────────────────────────\n");
    printf("  Ping (RTT) cur/avg/min/max : " ANSI_BOLD
           "%.1f / %.1f / %.1f / %.1f ms" ANSI_RESET "\n",
           s.pingMs.count ? s.pingMs.buf[(s.pingMs.pos-1+RollingStats::N)%RollingStats::N] : 0.f,
           s.pingMs.avg(), s.pingMs.min(), s.pingMs.max());
    if (s.hasSnap)
        printf("  Server bytes sent this tick : " ANSI_BOLD
               "%u" ANSI_RESET "  recv : " ANSI_BOLD "%u" ANSI_RESET "\n",
               snap.bytesSentThisTick, snap.bytesRecvThisTick);

    printf("\n" ANSI_DIM "  Refresh: ~4Hz    Press Ctrl+C to exit\n" ANSI_RESET);
    fflush(stdout);
}

// -------------------------------------------------------------
void DebugOverlay::init() {
    printf(ANSI_CLEAR);

    auto now = Clock::now();

    state_.startTime = now;
    lastSample_ = now;
    lastRender_ = now;
    lastQuery_  = now;
}

void DebugOverlay::onChunkReceived() {
    state_.totalChunksRecv++;
    state_.totalBlocksRecv += CHUNK_VOLUME;

    chunksSinceSample_++;
    bytesSinceSample_ += sizeof(PKT_S_ChunkData);
}

void DebugOverlay::update(Client& client) {
    constexpr float RENDER_INTERVAL_S = 0.25f;
    constexpr float SAMPLE_INTERVAL_S = 1.0f;
    constexpr float QUERY_INTERVAL_S  = 0.25f;

    auto now = Clock::now();

    if (client.hasPendingDebugSnapshot()) {
        auto snap = client.popDebugSnapshot();

        float rtt = std::chrono::duration<float, std::milli>(
                        now - lastQuery_).count();

        state_.lastSnap = snap;
        state_.hasSnap = true;

        state_.pingMs.push(rtt);
        state_.tickRate.push((float)snap.tickRateHz);
        state_.tickBudget.push(snap.tickBudgetUsedPct);
        state_.workerQueue.push((float)snap.workerQueueDepth);

        bytesSinceSample_ += snap.bytesRecvThisTick;
    }

    float sinceQuery =  std::chrono::duration<float>(
                            now - lastQuery_).count();
    if (sinceQuery >= QUERY_INTERVAL_S) {
        client.sendDebugQuery();
        lastQuery_ = now;
    }

    float sinceSample = std::chrono::duration<float>(
                            now - lastSample_).count();

    if (sinceSample >= SAMPLE_INTERVAL_S) {
        float bps =
            float(chunksSinceSample_ * CHUNK_VOLUME)
            / sinceSample;

        float cps =
            float(chunksSinceSample_)
            / sinceSample;

        float bwKB =
            float(bytesSinceSample_)
            / sinceSample
            / 1024.f;

        state_.blocksPerSec.push(bps);
        state_.chunksPerSec.push(cps);
        state_.bwKBps.push(bwKB);

        chunksSinceSample_ = 0;
        bytesSinceSample_  = 0;

        lastSample_ = now;
    }

    float sinceRender =
        std::chrono::duration<float>(
            now - lastRender_).count();

    if (sinceRender >= RENDER_INTERVAL_S) {
        state_.elapsedS =
            std::chrono::duration<float>(
                now - state_.startTime).count();

        renderDashboard(state_);

        lastRender_ = now;
    }
}