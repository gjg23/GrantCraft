// bench/gen_bench.cpp

#include "bench/gen_bench.hpp"
#include "generation/chunk_worker_pool.hpp"
#include "world/chunk.hpp"

#include <chrono>
#include <thread>
#include <cstdio>
#include <vector>

#include "generation/gpu_terrain/kernel.cuh"

constexpr uint32_t GPU_BATCH_SIZE = 1;

static GenBenchResult benchOneBackend(GenBackend backend, int radius,
                                       const std::string& label) {
    using Clock = std::chrono::steady_clock;

    printf("[GenBench] %s — building chunk list (radius %d)...\n",
           label.c_str(), radius);

    if (backend == GenBackend::GPU)
        uploadTerrainParams(makeTerrainParams());

    ChunkWorkerPool pool(backend);
    pool.init();

    std::vector<ChunkCoord> coords;
    for (int dx = -radius; dx <= radius; ++dx)
    for (int dy = 0;       dy <= radius; ++dy)
    for (int dz = -radius; dz <= radius; ++dz)
        coords.push_back({ dx, dy, dz });

    const uint32_t total = static_cast<uint32_t>(coords.size());
    printf("[GenBench] %s — submitting %u chunks...\n", label.c_str(), total);

    for (const auto& c : coords)
        pool.submit(c, 0);

    // Time from submission to last chunk drained
    uint32_t submitted = 0;
    uint32_t received  = 0;
    auto     start     = Clock::now();

    while (received < total) {
        uint32_t inFlight = submitted - received;
        if (submitted < total && inFlight < GPU_BATCH_SIZE) {
            uint32_t toSubmit = std::min(GPU_BATCH_SIZE - inFlight, total - submitted);
            for (uint32_t i = 0; i < toSubmit; ++i)
                pool.submit(coords[submitted++], 0);
        }
        if (backend == GenBackend::GPU)
            pool.pollGPU();

        auto batch = pool.drainCompleted();
        received  += static_cast<uint32_t>(batch.size());

        if (received < total)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    float elapsedS = std::chrono::duration<float>(Clock::now() - start).count();
    pool.shutdown();

    GenBenchResult r;
    r.backend         = label;
    r.chunksGenerated = total;
    r.totalBlocks     = (uint64_t)total * CHUNK_VOLUME;
    r.totalTimeS      = elapsedS;
    r.chunksPerSec    = elapsedS > 0.f ? (float)total / elapsedS : 0.f;
    r.blocksPerSecM   = elapsedS > 0.f
        ? (float)r.totalBlocks / elapsedS / 1e6f : 0.f;

    printf("[GenBench] %s done: %.3fs  %.2f Mblocks/s\n",
           label.c_str(), elapsedS, r.blocksPerSecM);
    return r;
}

std::vector<GenBenchResult> runGenBench(const GenBenchConfig& cfg) {
    std::vector<GenBenchResult> results;

    if (cfg.runCPU)
        results.push_back(benchOneBackend(GenBackend::CPU, cfg.radius, "CPU"));

    if (cfg.runGPU) {
        results.push_back(benchOneBackend(GenBackend::GPU, cfg.radius, "GPU"));
    }

    printGenBenchReport(results);
    if (!cfg.csvPath.empty()) writeGenBenchCSV(cfg.csvPath, results);
    return results;
}