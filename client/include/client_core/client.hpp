#pragma once
// ------------------------------------------------------------------
// client.hpp
// Manages the client side of the network connection.
// Responsibilities:
//   - Connect to a server (local or remote)
//   - Send player input each frame
//   - Receive and store the latest world snapshot from the server
// ------------------------------------------------------------------

#include "net/net_common.hpp"
#include "net/debug_packets.hpp"
#include "world/chunk.hpp"
#include "game_core/remote_player.hpp"
#include "game_core/chunk_cache.hpp"

#include <enet/enet.h>

#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <climits>


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

    void tick();

    void sendInput(float x, float y, float z, float yaw, float pitch);

    ChunkCache::TierDelta updatePlayerChunk(float x, float y, float z);

    void disconnect();

    bool isConnected()    const { return m_connected; }
    uint32_t getMyId()    const { return m_myId; }

    // ---- Chunk access ------------------------------------------------
    // Returns nullptr if the chunk is not cached at all.
    const CachedChunk* getChunk(const ChunkCoord& c) const {
        std::lock_guard<std::mutex> lk(m_chunkMutex);
        return m_cache.get(c);
    }

    bool copyChunkBlocks(const ChunkCoord& c, std::vector<BlockType>& out) const;

    // Drain newly-received chunks since last call
    struct NewChunkEvent {
        ChunkCoord coord;
        ChunkTier  tier;
    };
    std::vector<NewChunkEvent> drainNewChunks() {
        std::lock_guard<std::mutex> lk(m_chunkMutex);
        std::vector<NewChunkEvent> out = std::move(m_newChunkEvents);
        m_newChunkEvents.clear();
        return out;
    }

    void markRendererResident(const ChunkCoord& c) {
        std::lock_guard<std::mutex> lk(m_chunkMutex);
        m_cache.markRendererResident(c);
    }

    void markRendererReleased(const ChunkCoord& c) {
        std::lock_guard<std::mutex> lk(m_chunkMutex);
        m_cache.markRendererReleased(c);
    }

    // Configure distances (call before connect or any time)
    void setRenderDistance    (int r) {
        std::lock_guard<std::mutex> lk(m_chunkMutex);
        m_cache.setRenderDistance(r);
    }
    void setSimulationDistance(int s) {
        std::lock_guard<std::mutex> lk(m_chunkMutex);
        m_cache.setSimulationDistance(s);
    }

    // ---- Remote players ----------------------------------------------
    const std::unordered_map<uint32_t, RemotePlayer>& getRemotePlayers() const {
        return m_remotePlayers;
    }

    // ---- Debug -------------------------------------------------------
    void                  sendDebugQuery();
    bool                  hasPendingDebugSnapshot() const;
    PKT_S_DebugSnapshot   popDebugSnapshot();

private:
    ENetHost* m_host            = nullptr;
    ENetPeer* m_peer            = nullptr;
    bool      m_connected       = false;
    uint32_t  m_myId            = 0;
    char      m_username[16]    = {};

    // Latest positions received from the server, keyed by player ID
    std::unordered_map<uint32_t, RemotePlayer> m_remotePlayers;

    // ---- Chunk cache -------------------------------------------------
    mutable std::mutex          m_chunkMutex;
    ChunkCache                  m_cache;
    std::vector<NewChunkEvent>  m_newChunkEvents;

    // Track last player chunk so we only call onPlayerMoved on boundary cross
    ChunkCoord m_playerChunk{ INT_MAX, INT_MAX, INT_MAX };

    void onReceive(ENetPacket* packet);

    // ---- debug stats ----
    bool                m_hasDebugSnapshot = false;
    PKT_S_DebugSnapshot m_debugSnapshot{};
};