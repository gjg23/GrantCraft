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
    int     width   = 0;
    int     height  = 0;
    bool    cursorLocked = true;

    bool init(int, int, const char* title);
    bool isOpen() const;

    // Swap front/back buffers and poll events
    void swapAndPoll();

    // free cursor vs first person cam
    void lockCursor();
    void unlockCursor();
    void toggleCursor();
    bool isCursorLocked() const { return cursorLocked; }

    // Returns aspect ratio using the actual framebuffer dimensions.
    float aspectRatio() const {
        if (height == 0) return 16.0f / 9.0f;
        return (float)width / (float)height;
    }

    void destroy();
};
