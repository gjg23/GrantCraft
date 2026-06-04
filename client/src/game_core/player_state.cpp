// client/src/game_core/player_state.cpp
#include "game_core/player_state.hpp"
#include <glm/glm.hpp>
#include <algorithm>

PlayerState::PlayerState(glm::vec3 startPos) {
    state.position = startPos;
    cam = Camera(startPos + glm::vec3(0.f, EYE_HEIGHT, 0.f));
}

void PlayerState::update() {
    state.velocity  *= 0.8f;  // drag — replace with real physics later
    state.position  += state.velocity * dt;
    cam.position     = state.position + glm::vec3(0.f, EYE_HEIGHT, 0.f);
    state.yaw        = cam.yaw;
    state.pitch      = cam.pitch;
}

void PlayerState::moveForward() {
    glm::vec3 flat = glm::normalize(glm::vec3(cam.getFront().x, 0.f, cam.getFront().z));
    state.position += flat * speed * dt;
}
void PlayerState::moveBack() {
    glm::vec3 flat = glm::normalize(glm::vec3(cam.getFront().x, 0.f, cam.getFront().z));
    state.position -= flat * speed * dt;
}
void PlayerState::moveLeft() {
    state.position -= glm::normalize(glm::cross(cam.getFront(), {0.f, 1.f, 0.f})) * speed * dt;
}
void PlayerState::moveRight() {
    state.position += glm::normalize(glm::cross(cam.getFront(), {0.f, 1.f, 0.f})) * speed * dt;
}
void PlayerState::moveUp()   { state.position.y += speed * dt; }
void PlayerState::moveDown() { state.position.y -= speed * dt; }

void PlayerState::setSprint(bool on) {
    if (on == sprinting) return;
    sprinting = on;
    speed = on ? speed * sprintMag : speed / sprintMag;
}