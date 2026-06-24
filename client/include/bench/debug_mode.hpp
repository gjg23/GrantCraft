#pragma once

#include "net/debug_packets.hpp"

#include <chrono>
#include <cstdint>

class Client;

struct RollingStats {
    static constexpr int N = 60;  // 60 samples = ~60s at 1Hz
    float buf[N] = {};
    int   pos    = 0;
    int   count  = 0;

    void push(float v) {
        buf[pos] = v;
        pos = (pos + 1) % N;
        if (count < N) ++count;
    }
    float avg() const {
        if (!count) return 0.f;
        float s = 0; for (int i=0;i<count;++i) s+=buf[i];
        return s/count;
    }
    float min() const {
        if (!count) return 0.f;
        float m=buf[0]; for(int i=1;i<count;++i) if(buf[i]<m)m=buf[i];
        return m;
    }
    float max() const {
        if (!count) return 0.f;
        float m=buf[0]; for(int i=1;i<count;++i) if(buf[i]>m)m=buf[i];
        return m;
    }
};

struct DashState {
    // From debug snapshots
    PKT_S_DebugSnapshot lastSnap{};
    bool hasSnap = false;

    // Client-side
    uint64_t totalChunksRecv  = 0;
    uint64_t totalBlocksRecv  = 0;
    uint64_t chunksDelta      = 0;   // since last render frame
    uint64_t bytesDelta       = 0;

    // Rolling stats (updated at 1Hz)
    RollingStats blocksPerSec;
    RollingStats chunksPerSec;
    RollingStats pingMs;
    RollingStats bwKBps;
    RollingStats tickRate;
    RollingStats tickBudget;
    RollingStats workerQueue;

    float elapsedS = 0.f;
    std::chrono::steady_clock::time_point startTime;
};

class DebugOverlay {
public:
    void init();
    void onChunkReceived();
    void update(Client& client);

private:
    DashState state_;

    using Clock = std::chrono::steady_clock;

    Clock::time_point lastSample_;
    Clock::time_point lastRender_;
    Clock::time_point lastQuery_;

    uint64_t chunksSinceSample_ = 0;
    uint64_t bytesSinceSample_ = 0;
};