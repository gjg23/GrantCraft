#pragma once
// bench/gen_bench.hpp

#include "bench/metrics.hpp"
#include <string>

struct GenBenchConfig {
    int         radius  = 5;
    bool        runCPU  = true;
    bool        runGPU  = false;
    std::string csvPath;
};

// Runs enabled backends sequentially and prints a summary.
std::vector<GenBenchResult> runGenBench(const GenBenchConfig& cfg);