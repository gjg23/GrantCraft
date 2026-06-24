// main.cpp (server)

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <cstring>
#include <enet/enet.h>

#include "server/server.hpp"

int main(int argc, char* argv[]) {
    if (enet_initialize() != 0) {
        fprintf(stderr, "Failed to initialize ENet\n");
        exit(EXIT_FAILURE);
    }

    uint16_t   port = 7777;
    ServerMode mode = ServerMode::CPU;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--gpu") == 0)
            mode = ServerMode::GPU;
        else if (strcmp(argv[i], "--cpu") == 0)
            mode = ServerMode::CPU;
        else
            port = static_cast<uint16_t>(std::atoi(argv[i]));
    }

    printf("[Server] Mode: %s, Port: %u\n", mode == ServerMode::GPU ? "GPU" : "CPU", port);

    // make server
    Server server;
    if (!server.init(port, mode)) {
        enet_deinitialize();
        fprintf(stderr, "Server init failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("[Server] Running on port %d\n", port);

    // ------------------------------------------------------------------
    // Fixed-timestep server loop
    // 20 ticks per second mirrors Minecraft's server tick rate
    // ------------------------------------------------------------------
    constexpr float TICK_RATE = 20.0f;
    constexpr float TICK_DT   = 1.0f / TICK_RATE;
    auto targetDuration = std::chrono::duration<float>(TICK_DT);

    // start server loop
    while (server.isRunning()) {
        auto frameStart = std::chrono::steady_clock::now();

        server.tick(TICK_DT);

        // Sleep for the remainder of the tick budget so we don't
        // burn 100% CPU just waiting for network events
        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        auto sleep   = targetDuration - elapsed;
        if (sleep > std::chrono::duration<float>(0)) {
            std::this_thread::sleep_for(sleep);
        }
    }

    server.shutdown();
    enet_deinitialize();
    return 0;
}