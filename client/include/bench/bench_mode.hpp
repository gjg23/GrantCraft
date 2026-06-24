#pragma once
#include <cstdint>

void runBenchMode(const char* host, uint16_t port,
                  const char* username, int radiusOverride = -1);