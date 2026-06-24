// local_server.cpp

#include "client_core/local_server.hpp"
#include <cstdio>
#include <chrono>
#include <thread>
#include <enet/enet.h>

bool LocalServer::start(uint16_t port, ServerMode mode) {
    if (!m_server.init(port, mode)) {
        fprintf(stderr, "[LocalServer] Failed to start on port %u\n", port);
        return false;
    }
    m_running = true;
    // Launch the server tick loop on its own thread so the client
    // render loop is not blocked
    m_thread = std::thread(&LocalServer::runLoop, this);
    printf("[LocalServer] Started on port %u\n", port);
    return true;
}

void LocalServer::stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_server.shutdown();
}

void LocalServer::runLoop() {
    constexpr float TICK_DT = 1.0f / 20.0f;
    auto targetDuration = std::chrono::duration<float>(TICK_DT);

    while (m_running.load()) {
        auto frameStart = std::chrono::steady_clock::now();

        m_server.tick(TICK_DT);

        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        auto sleep   = targetDuration - elapsed;
        if (sleep > std::chrono::duration<float>(0)) {
            std::this_thread::sleep_for(sleep);
        }
    }
}