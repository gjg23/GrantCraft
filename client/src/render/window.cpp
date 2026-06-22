// window.cpp
#include "render/window.hpp"
#include <cstdio>

// Stored so GLFW callbacks can reach the Window instance
static Window* s_window = nullptr;

static void mouseButtonCallback(GLFWwindow* win, int button, int action, int mods) {
    if (!s_window) return;
    
    // Click anywhere on the window while unlocked -> relock
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (!s_window->isCursorLocked()) s_window->lockCursor();
    }
}

static void focusCallback(GLFWwindow* win, int focused) {
    if (!s_window) return;
    
    // window minimized so unlock cursor
    if (!focused) s_window->unlockCursor();
}

// Keep the window viewport update when moving window around
static void framebufferSizeCallback(GLFWwindow* win, int w, int h) {
    if (!s_window) return;          // no window
    if (w == 0 || h == 0) return;   // minimised
    s_window->width  = w;
    s_window->height = h;
    glViewport(0, 0, w, h);
}

// ------------------------------------------
// Window lifecycle functions
// ------------------------------------------
bool Window::init(int w, int h, const char* title) {
    width = w; height = h;

    // Request OpenGL 3.3 Core profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);

    handle = glfwCreateWindow(w, h, title, nullptr, nullptr);
    if (!handle) { fputs("glfwCreateWindow failed\n", stderr); return false; }

    glfwMakeContextCurrent(handle);

    // Load OpenGL function pointers via GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fputs("GLAD init failed\n", stderr); return false;
    }

    // Query the framebuffer size
    int fbW, fbH;
    glfwGetFramebufferSize(handle, &fbW, &fbH);
    width  = fbW;
    height = fbH;
    glViewport(0, 0, fbW, fbH);

    glEnable(GL_DEPTH_TEST);    // 3d ordering
    glEnable(GL_CULL_FACE);     // cull faces
    glCullFace(GL_BACK);

    if (glfwRawMouseMotionSupported())
        // capture mouse
        glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    // Register callbacks
    s_window = this;
    glfwSetMouseButtonCallback(handle, mouseButtonCallback);
    glfwSetWindowFocusCallback(handle, focusCallback);
    glfwSetFramebufferSizeCallback(handle, framebufferSizeCallback);

    glfwShowWindow(handle);
    return true;
}

bool Window::isOpen() const {
    return !glfwWindowShouldClose(handle);
}

void Window::swapAndPoll() {
    glfwSwapBuffers(handle);
    glfwPollEvents();
}

void Window::destroy() {
    glfwDestroyWindow(handle);
    glfwTerminate();
}

// ------------------------------------------
// Cursor functions
// ------------------------------------------
void Window::lockCursor() {
    if (!glfwGetWindowAttrib(handle, GLFW_FOCUSED)) return;
    // lock
    glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    // Warp mouse to center
    glfwSetCursorPos(handle, width * 0.5, height * 0.5);
    cursorLocked = true;
}

void Window::unlockCursor() {
    glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    cursorLocked = false;
}

void Window::toggleCursor() {
    if (cursorLocked) unlockCursor();
    else lockCursor();
}
