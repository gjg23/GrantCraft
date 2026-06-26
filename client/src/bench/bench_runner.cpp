// bench/bench_runner.cpp

#include "bench/bench_runner.hpp"
#include "bench/gen_bench.hpp"
#include "bench/load_test.hpp"

void runBench(const BenchArgs& args) {
    // Terrain generation throughput test
    if (args.mode == BenchArgs::Mode::Gen) {
        printf("[Bench] Generation test — radius %d, CPU=%s GPU=%s\n",
               args.radius,
               args.genCPU ? "yes" : "no",
               args.genGPU ? "yes" : "no");

        GenBenchConfig cfg;
        cfg.radius  = args.radius;
        cfg.runCPU  = args.genCPU;
        cfg.runGPU  = args.genGPU;
        if (args.csvPath) cfg.csvPath = args.csvPath;
        runGenBench(cfg);
    }
    // Server client load testing
    else {
        printf("[Bench] Load test — %d players, radius %d, %.0fs, GPU server=%s\n",
               args.players, args.radius, args.durationS,
               args.gpuServer ? "yes" : "no");

        LoadTestConfig cfg;
        cfg.playerCount   = (uint32_t)args.players;
        cfg.renderRadius  = args.radius;
        cfg.testDurationS = args.durationS;
        cfg.spawnRadius   = args.spawnRadius;
        cfg.stationaryS   = args.stationaryS;
        cfg.port          = args.port;
        cfg.serverMode    = args.gpuServer ? ServerMode::GPU : ServerMode::CPU;
        if (args.csvPath) cfg.csvPath = args.csvPath;
        runLoadTest(cfg);
    }
}