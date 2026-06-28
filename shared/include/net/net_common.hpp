#pragma once
// net_common.hpp
// Packet type IDs
// Packet structs
// Channel numbers

#include <cstdint>
#include <cstring>
#include <enet/enet.h>
#include <world/chunk.hpp>
#include "world/shared_player_state.hpp"

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

    // Debug
    C_DEBUG_QUERY       = 20,
    S_DEBUG_SNAPSHOT    = 21,
};


// ----- Packet payload structs -----
// keep old data to memcpy into enet packet buffers
// Client -> Server: first message after connection
#pragma pack(push,1)    // keep alignment
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

// Chunks and encoding
struct PKT_S_ChunkData {
    PacketType type       = PacketType::S_CHUNK_DATA;
    int32_t    cx, cy, cz;
    uint32_t   dataSize;
    uint8_t    encoding;
};
#pragma pack(pop)

enum class ChunkEncoding : uint8_t { Raw = 0, RLE = 1, Uniform = 2, LZ4 = 3 };

inline uint32_t rleEncodeBlocks(const BlockType* src, uint32_t count,
                                uint8_t* out, uint32_t outCapacity)
{
    uint32_t outPos = 0, i = 0;
    while (i < count) {
        BlockType val    = src[i];
        uint16_t  runLen = 1;
        while (i + runLen < count && src[i + runLen] == val && runLen < 0xFFFFu)
            ++runLen;
        if (outPos + sizeof(uint16_t) + sizeof(BlockType) > outCapacity)
            return 0; // overflow
        memcpy(out + outPos, &runLen, sizeof(runLen)); outPos += sizeof(runLen);
        memcpy(out + outPos, &val,    sizeof(val));    outPos += sizeof(val);
        i += runLen;
    }
    return outPos;
}

inline bool rleDecodeBlocks(const uint8_t* in, uint32_t inSize,
                            BlockType* out, uint32_t outCount)
{
    uint32_t inPos = 0, outPos = 0;
    while (inPos + sizeof(uint16_t) + sizeof(BlockType) <= inSize) {
        uint16_t  runLen = 0;
        BlockType val    = {};
        memcpy(&runLen, in + inPos, sizeof(runLen)); inPos += sizeof(runLen);
        memcpy(&val,    in + inPos, sizeof(val));    inPos += sizeof(val);
        if (outPos + runLen > outCount) return false;
        for (uint16_t j = 0; j < runLen; ++j)
            out[outPos++] = val;
    }
    return outPos == outCount;
}

// LZ4 bound
inline constexpr uint32_t LZ4_MAX_INPUT_SIZE = 0x7E000000u;
inline constexpr uint32_t lz4CompressBound(uint32_t inputSize) {
    return (inputSize > LZ4_MAX_INPUT_SIZE)
        ? 0u
        : inputSize + (inputSize / 255u) + 16u;
}


// ----- helper: wrap POD struct in ENet packet -----
template<typename T>
ENetPacket* makePacket(const T& data, uint32_t flags) {
    // enet_packet_create copies the buffer so passing stack data is safe
    return enet_packet_create(&data, sizeof(T), flags);
}