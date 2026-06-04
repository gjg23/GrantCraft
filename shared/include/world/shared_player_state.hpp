#pragma once
// ------------------------------------------------------------------
// Shared player data structure in input model
// ------------------------------------------------------------------

#include <glm/glm.hpp>

struct SharedPlayerState {
    uint32_t  id       = 0;
    glm::vec3 position = {0, 0, 0};
    glm::vec3 velocity = {0, 0, 0};
    float     yaw      = 0.0f;
    float     pitch    = 0.0f;
};