// ================================================================
// bench_main.cpp
// Called from main.cpp after flag parsing.
// ================================================================

#include "bench/bench_mode.hpp"
#include "bench/debug_mode.hpp"
#include <cstring>

// Forward declared — implemented below, called from your main.cpp
void runBenchMode(const char* host, uint16_t port,
                  const char* username, int radiusOverride);

void runDebugMode(const char* host, uint16_t port,
                  const char* username);