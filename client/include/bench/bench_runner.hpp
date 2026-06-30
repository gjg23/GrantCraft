#pragma once
// bench/bench_runner.hpp
// Single entry point called from main()

#include <cstdint>

struct BenchArgs {
    enum class Mode { Gen, Load } mode = Mode::Gen;

    // shared
    int         radius      = 4;
    const char* csvPath     = nullptr;

    // gen bench
    bool genCPU = true;
    bool genGPU = false;

    // load test
    uint16_t    port          = 5070;
    int         players       = 5;
    float       durationS     = 120.f;
    float       spawnRadius   = 80.f;
    float       stationaryS   = 10.f;
    bool        gpuServer     = false;
};

// Called from main() after flag parsing. Blocks until test is done.
void runBench(const BenchArgs& args);