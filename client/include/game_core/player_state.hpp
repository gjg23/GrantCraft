#pragma once
// ------------------------------------------------------------------
// client/include/game_core/player_state.hpp
// Client-side player state.
// Wraps SharedPlayerState (the data that crosses the network) and
// adds movement logic, camera sync, and dt — none of which the
// server needs.
// ------------------------------------------------------------------

#include "world/shared_player_state.hpp"
#include "game_core/camera.hpp"
#include <glm/glm.hpp>

class PlayerState {
public:
    static constexpr float EYE_HEIGHT = 1.62f;

    SharedPlayerState state;  // the part that gets sent to the server
    Camera            cam;

    explicit PlayerState(glm::vec3 startPos);

    // Call once per frame with the frame delta before moving
    void setDt(float delta) { dt = delta; }

    // Integrate velocity into position, sync camera
    void update();

    void moveForward();
    void moveBack();
    void moveLeft();
    void moveRight();
    void moveUp();
    void moveDown();
    void setSprint(bool on);

    // Convenience accessors so call sites don't need to reach into state
    glm::vec3& position() { return state.position; }
    float&     yaw()      { return state.yaw; }
    float&     pitch()    { return state.pitch; }

private:
    float dt          = 0.f;
    float speed       = 100.f;
    float sprintMag   = 2.f;
    bool  sprinting   = false;
    bool  isGrounded  = false;
};