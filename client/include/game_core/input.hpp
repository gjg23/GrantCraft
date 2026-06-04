#pragma once
// -------------------------------------------------------
// input.h
// Handles keyboard (WASD) and mouse input each frame.
// -------------------------------------------------------

#include "game_core/player_state.hpp"
#include "render/window.hpp"
#include <GLFW/glfw3.h>

struct Input {
    void init(GLFWwindow* window);
    void update(GLFWwindow* window, PlayerState& player, bool cursorLocked);

    float dt = 0.f;
    bool escapePressed = false;
private:
    bool escWasPressed = false;
};
