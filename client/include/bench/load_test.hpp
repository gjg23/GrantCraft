#pragma once
// bench/load_test.hpp

#include "bench/metrics.hpp"
#include "client_core/local_server.hpp"

#include <string>
#include <cstdint>

struct LoadTestConfig {
    uint32_t    playerCount   = 4;
    int         renderRadius  = 4;
    float       testDurationS = 60.f;
    float       stationaryS   = 10.f;
    float       moveSpeed     = 4.5f;
    float       spawnRadius   = 80.f;
    uint16_t    port          = 5070;
    ServerMode  serverMode    = ServerMode::CPU;
    std::string csvPath;
};

// Blocks for testDurationS, then returns the full report.
LoadTestReport runLoadTest(const LoadTestConfig& cfg);