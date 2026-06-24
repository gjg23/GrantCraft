#pragma once
// -------------------------------------------------
// server/ecs/components.hpp
// defines structures all entities share in game
// -------------------------------------------------

#include <cstdint>
#include <glm/glm.hpp>
#include <enet/enet.h>

using EntityId = uint32_t;
constexpr EntityId NULL_ENTITY = 0;

struct PositionComp {
    glm::vec3 pos = {0, 0, 0};
    glm::vec3 vel = {0, 0, 0};
    float yaw     = 0.f;
    float pitch   = 0.f;
};

struct NetworkComp {
    ENetPeer* peer          = nullptr;
    uint32_t  playerId      = 0;
    char      username[16]  = {};
};

// component for future code
struct ChunkInterestComp {
    int renderDistance = 8;
};