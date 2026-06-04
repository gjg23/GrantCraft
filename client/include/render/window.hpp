#pragma once
// ------------------------------------------------------------------
// window.hpp
// Just makes GLFW window for glad context
// Used by client to show the game
// ------------------------------------------------------------------

#include <glad.h>
#include <GLFW/glfw3.h>

struct Window {
    GLFWwindow* handle = nullptr;
    int width, height;
    bool focused = true;    // lock cursor or not

    bool init(int w, int h, const char* title);
    bool isOpen() const;

    // Swap front/back buffers and poll events
    void swapAndPoll();

    // free cursor vs first person cam
    void lockCursor();
    void unlockCursor();
    void toggleCursor();
    bool isCursorLocked() const { return focused; }

    void destroy();
};
