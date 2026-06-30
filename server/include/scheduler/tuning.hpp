#pragma once
// ======================================================================
// scheduler/tuning.hpp
// All tunable knobs and dials in one struct
// ======================================================================

#include <cstdint>

struct ServerTuning {
    // ---- Budget ----
    float  totalTickBudgetMs    = 45.f;
    float  reservedHeadroomMs   = 4.f;
    float  phaseBudgetWeights[6]= {1.f, 3.f, 3.f, 2.f, 15.f, 0.5f};

    // ---- Network ----
    uint32_t maxChunksPerPeerPerTick     = 24;     // dynamic
    uint32_t globalMaxChunkBytesPerTick  = UINT32_MAX;
    uint32_t perPeerMaxBytesPerTick      = UINT32_MAX;
    int      playerStateBroadcastRadius  = 384;    // sim dist. x chunk
    int      playerStateSendRate         = 1;
    // compression-effort threshold: 0 = always LZ4, 1 = always RLE
    // note: (just use smallest size?)
    float    compressionEffortThreshold = 0.5f;

    // ---- Generation ----
    uint32_t maxChunksSubmittedPerTick   = 64;     // dynamic
    uint32_t maxGpuInFlight              = 16;
    int      workerThreadCount           = -1;     // -1 = hw_concurrency - 1
    int      taskPoolThreadCount         = -1;

    // ---- Interest ----
    int      renderDistance              = 8;    // These would be set by client settings
    int      simulationDistance          = 12;
    uint32_t maxInterestRecomputesPerTick= UINT32_MAX;
    uint32_t maxNewSubscriptionsPerTick  = 500;

    // ---- Policy weights ----
    float    priorityClassWeights[4]     = {100.f, 10.f, 1.f, 0.1f};
    float    distanceFalloff             = 1.f;    // multiplier on sortKey
    float    starvationBoostRate         = 0.0001f;// sortKey reduction per µs of age

    // ---- Adaptive controller gains ----
    float    targetUtilization           = 0.85f;
    float    hysteresisBand              = 0.05f;
    float    ewmaAlpha                   = 0.2f;
    float    deadlineMissPenalty         = 2.f;
    float    knobAdjustmentStepSize      = 0.1f;

    // ---- Limits for dynamic knobs (adaptive never exceeds these) ----
    uint32_t maxChunksPerPeerUpper       = 64;
    uint32_t maxChunksSubmittedUpper     = 256;
};