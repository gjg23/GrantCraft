// input.cpp
#include "game_core/input.hpp"
#include <iostream>
#include <GLFW/glfw3.h>

// store last deltas computed in update()
static float  s_dx    = 0.f;
static float  s_dy    = 0.f;
static double s_lastX = 0.0;
static double s_lastY = 0.0;
static bool   s_first = true;

void Input::init(GLFWwindow* window) {
    // Register mouse callback
    glfwSetCursorPosCallback(window, [](GLFWwindow*, double xpos, double ypos){
        if (s_first) { s_lastX = xpos; s_lastY = ypos; s_first = false; return; }
        s_dx = (float)(xpos - s_lastX);
        s_dy = (float)(ypos - s_lastY);
        s_lastX = xpos; s_lastY = ypos;
    });
}

// input.cpp — update() signature change only, logic unchanged
void Input::update(GLFWwindow* window, PlayerState& player, bool cursorLocked) {
    player.setDt(dt);

    // Escape toggles cursor lock rather than quitting
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escWasPressed) {
        escapePressed  = true;
        escWasPressed  = true;
    } else {
        escapePressed  = false;
    }
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE)
        escWasPressed = false;
    
    if (!cursorLocked) return;

    bool w    = glfwGetKey(window, GLFW_KEY_W)            == GLFW_PRESS;
    bool s    = glfwGetKey(window, GLFW_KEY_S)            == GLFW_PRESS;
    bool a    = glfwGetKey(window, GLFW_KEY_A)            == GLFW_PRESS;
    bool d    = glfwGetKey(window, GLFW_KEY_D)            == GLFW_PRESS;
    bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;

    if (w) { player.moveForward(); }
    if (s) player.moveBack();
    if (a) player.moveLeft();
    if (d) player.moveRight();
    if (glfwGetKey(window, GLFW_KEY_SPACE)      == GLFW_PRESS) player.moveUp();
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) player.moveDown();

    bool moving = w || s || a || d;
    if (ctrl && moving) player.setSprint(true);
    else if (!moving)   player.setSprint(false);

    if (s_dx != 0.f || s_dy != 0.f) {
        player.cam.rotate(s_dx, s_dy);
        // printf("[Client] Camera delta (%.2f, %.2f) → yaw %.2f pitch %.2f\n", s_dx, s_dy, player.cam.yaw, player.cam.pitch);
        s_dx = s_dy = 0.f;
    }

    // Sync position and orientation after all input is applied
    player.update();
}
