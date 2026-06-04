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

// ------------------------------------------
// Window lifecycle functions
// ------------------------------------------
bool Window::init(int w, int h, const char* title) {
    width = w; height = h;

    // Request OpenGL 3.3 Core profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    handle = glfwCreateWindow(w, h, title, nullptr, nullptr);
    if (!handle) { fputs("glfwCreateWindow failed\n", stderr); return false; }

    glfwMakeContextCurrent(handle);

    // Load OpenGL function pointers via GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fputs("GLAD init failed\n", stderr); return false;
    }

    glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST);   // needed for correct block ordering
    glEnable(GL_CULL_FACE);    // cull back faces (mesh builder emits front-facing quads)

    // Capture mouse for first-person look
    glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    
    lockCursor();
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
    glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    // Warp to center so the first delta after relock isn't a huge jump
    glfwSetCursorPos(handle, width * 0.5, height * 0.5);
    focused = true;
}

void Window::unlockCursor() {
    glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    focused = false;
}

void Window::toggleCursor() {
    if (focused) unlockCursor();
    else         lockCursor();
}
