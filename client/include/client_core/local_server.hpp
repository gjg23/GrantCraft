#pragma once
// ------------------------------------------------------------------
// local_server.h
// Wraps the Server class so the client can spin up a server
// inside the same process for singleplayer.
//
// The local server runs on localhost on a fixed port.
// The client then connects to it exactly like a remote server.
// This means singleplayer uses the SAME code path as multiplayer,
// which prevents divergent bugs.
// ------------------------------------------------------------------

#include <thread>
#include <atomic>
#include "server/server.hpp"

class LocalServer {
public:
    // Start the server on a background thread
    bool start(uint16_t port = 7778, ServerMode mode = ServerMode::CPU);

    // Signal the server to stop and join the thread
    void stop();

    bool isRunning() const { return m_running.load(); }

private:
    Server              m_server;
    std::thread         m_thread;
    std::atomic<bool>   m_running{false};

    void runLoop();
};