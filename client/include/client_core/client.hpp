#pragma once
// ------------------------------------------------------------------
// client.h
// Manages the client side of the network connection.
// Responsibilities:
//   - Connect to a server (local or remote)
//   - Send player input each frame
//   - Receive and store the latest world snapshot from the server
// ------------------------------------------------------------------

#include <enet/enet.h>
#include <cstdint>
#include <unordered_map>
#include "net/net_common.hpp"
#include "game_core/remote_player.hpp"
#include "world/chunk.hpp"

#include <unordered_map>
#include <mutex>

class Client {
public:
    // ------------------------------------------------------------------
    // connect - attempt to connect to host:port
    // Blocks up to timeoutMs waiting for the handshake to complete.
    // Returns false if connection could not be established.
    // ------------------------------------------------------------------
    bool connect(const char* host, uint16_t port,
                 const char* username,
                 uint32_t timeoutMs = 3000);

    // ------------------------------------------------------------------
    // tick - pump ENet events, must be called every frame
    // ------------------------------------------------------------------
    void tick();

    // ------------------------------------------------------------------
    // sendInput - tell the server where we are this frame
    // Call after local player movement has been applied
    // ------------------------------------------------------------------
    void sendInput(float x, float y, float z,
                   float yaw, float pitch);

    void disconnect();

    bool isConnected()    const { return m_connected; }
    uint32_t getMyId()    const { return m_myId; }

    // Read-only snapshot of other players (for the renderer)
    const std::unordered_map<uint32_t, RemotePlayer>& getRemotePlayers() const {
        return m_remotePlayers;
    }

    // get and store chunks
    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> m_chunks;
    std::vector<ChunkCoord>                               m_newChunks;  // dirty list for mesher
    std::mutex                                            m_chunkMutex;

    const auto& getChunks()    const { return m_chunks; }
    std::vector<ChunkCoord> getAndClearNewChunks() {
        std::lock_guard<std::mutex> lk(m_chunkMutex);
        std::vector<ChunkCoord> out = std::move(m_newChunks);
        m_newChunks.clear();
        return out;
    }

private:
    ENetHost* m_host      = nullptr;
    ENetPeer* m_peer      = nullptr;
    bool      m_connected = false;
    uint32_t  m_myId      = 0;

    char m_username[16] = {};

    // Latest positions received from the server, keyed by player ID
    std::unordered_map<uint32_t, RemotePlayer> m_remotePlayers;

    void onReceive(ENetPacket* packet);
};