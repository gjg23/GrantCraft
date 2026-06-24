#pragma once

#include <cstdint>

struct WorkerPoolStats {
    uint32_t queueDepth    = 0;  // chunks in genQueue at last dispatch
    uint32_t gpuInFlight   = 0;  // GPU jobs not yet complete
    uint32_t completedLast = 0;  // chunks completed in last drain
};