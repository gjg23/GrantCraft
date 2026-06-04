#pragma once
// net_common.hpp
// Packet type IDs
// Packet structs
// Channel numbers

#include <cstdint>
#include <enet/enet.h>
#include <world/chunk.hpp>
#include "world/shared_player_state.hpp"

// how many blocks in one chunk column
constexpr int BLOCKS_PER_CHUNK = CHUNK_SIZE* CHUNK_SIZE * CHUNK_SIZE;

// ----- ENet Chanel layout -----
// Channel 0 = reliable ordered (login, spawn, world data)
// Chanel 1  = unreliable       (player pos)
constexpr uint8_t CHANNEL_RELIABLE   = 0;
constexpr uint8_t CHANNEL_UNRELIABLE = 1;
constexpr uint8_t CHANNEL_COUNT      = 2;


// ----- Packet type tag -----
enum class PacketType : uint8_t {
    // Server -> Client
    S_WELCOME       = 0,   // sent to a client when they successfully connect
    S_PLAYER_STATE  = 1,   // broadcast all players' positions each tick
    S_CHUNK_DATA    = 2,

    // Client -> Server
    C_JOIN          = 10,  // client introduces itself with a name
    C_PLAYER_INPUT  = 11,  // client sends its input / desired position each tick
};


// ----- Packet payload structs -----
// keep old data to memcpy into enet packet buffers
// Client -> Server: first message after connection
struct PKT_C_Join {
    PacketType type = PacketType::C_JOIN;
    char username[16];   // null-terminated, max 15 chars + null
};

// Server -> Client: welcome assigns the client its player ID
struct PKT_S_Welcome {
    PacketType type = PacketType::S_WELCOME;
    uint32_t   playerId;       // unique ID assigned by server
};

// Client -> Server: sent every tick with desired movement
struct PKT_C_PlayerInput {
    PacketType type = PacketType::C_PLAYER_INPUT;
    float x, y, z;      // client-predicted position (server validates)
    float yaw, pitch;   // look direction
};

struct PlayerNetState {
    uint32_t id;
    float    x, y, z;
    float    yaw, pitch;
};

// Server -> Client: broadcast up to 64 players per tick
struct PKT_S_PlayerState {
    PacketType  type = PacketType::S_PLAYER_STATE;
    uint8_t     playerCount;
    PlayerNetState players[64];
};

// Server -> Client: full block data for one chunk
// This is large (~65KB per chunk) so send reliable and only once per chunk
struct PKT_S_ChunkData {
    PacketType type = PacketType::S_CHUNK_DATA;
    int32_t    cx, cy, cz;                    // chunk coordinate
    uint32_t   blockCount = BLOCKS_PER_CHUNK;
    BlockType  blocks[BLOCKS_PER_CHUNK];
};


// ----- helper: wrap POD struct in ENet packet -----
template<typename T>
ENetPacket* makePacket(const T& data, uint32_t flags) {
    // enet_packet_create copies the buffer so passing stack data is safe
    return enet_packet_create(&data, sizeof(T), flags);
}