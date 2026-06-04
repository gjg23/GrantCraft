// ------------------------------------------------------------------
// client/main.cpp
// Entry point for the CLIENT
//
// Command-line modes:
//   game_client                    -> singleplayer (spins up local server)
//   game_client <host> <port>      -> multiplayer (join remote server)
// ------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Third party headers
#include <enet/enet.h>
#include <glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "client_core/local_server.hpp"
#include "client_core/client.hpp"
#include "render/renderer.hpp"
#include "render/window.hpp"
#include "game_core/camera.hpp"
#include "game_core/input.hpp"
#include "game_core/remote_player.hpp"

int main(int argc, char* argv[]) {
    // ------------------------------------------------------------------
    // Determine mode from arguments
    // ./game_client                        # singleplayer  (argc == 1)
    // ./game_client 192.168.1.5 25565      # multiplayer   (argc == 3)
    // ------------------------------------------------------------------
    bool        singleplayer = (argc < 3);
    const char* remoteHost = singleplayer ? "127.0.0.1" : argv[1];
    uint16_t    remotePort = singleplayer ? 7778        : static_cast<uint16_t>(atoi(argv[2]));
    if (argc != 1 && argc != 3) {
        fprintf(stderr, "Usage: %s [host addr] [host port]\n", argv[0]);
        return 1;
    }

    // Start ENet client
    printf("Starting ENet.\n");
    if (enet_initialize() != 0) {
        fprintf(stderr, "Failed to initialize ENet.\n");
        return 1;
    }

    // ------------------------------------------------------------------
    // Singleplayer: spin up a local server on a background thread
    // then connect to it just like any other server
    // ------------------------------------------------------------------
    LocalServer localServer;
    if (singleplayer) {
        printf("Starting local server.\n");
        if (!localServer.start(remotePort)) {
            enet_deinitialize();
            fprintf(stderr, "Staring local server failed.\n");
            exit(EXIT_FAILURE);
        }
    }

    // ------------------------------------------------------------------
    // Window + OpenGL setup (your existing Window class)
    // ------------------------------------------------------------------
    // Start glfw
    printf("Starting glfw.\n");
    if (!glfwInit()) {
        fprintf(stderr, "glfwInit failed.\n");
        exit(EXIT_FAILURE);
    }

    // Create game window
    Window win;
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(mon);
    if (!win.init(mode->width, mode->height, "Masters Project")) {
        fprintf(stderr, "window creation failed\n");
        enet_deinitialize();
        exit(EXIT_FAILURE);
    }

    // Opengl enable depth
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // ------------------------------------------------------------------
    // Game objects
    // ------------------------------------------------------------------
    PlayerState   localPlayer({0.5f, 64.f, 0.5f});  // client-side PlayerState with camera
    Input         input;
    input.init(win.handle);

    // Remote players populated by Client::onReceive — keyed by player ID
    std::unordered_map<uint32_t, RemotePlayer> remotePlayers;
    
    // renderer
    Renderer renderer;
    if (!renderer.init()) {
        fprintf(stderr, "Renderer init failed\n");
        exit(EXIT_FAILURE);
    }

    // ------------------------------------------------------------------
    // Connect to the server (local or remote)
    // ------------------------------------------------------------------
    printf("Connecting to %s:%u.\n", remoteHost, remotePort);
    Client client;
    if (!client.connect(remoteHost, remotePort, "Player1")) {
        fprintf(stderr, "Could not connect to server.\n");
        exit(EXIT_FAILURE);
    }

    // ------------------------------------------------------------------
    // Projection matrix - fixed FOV, updated on resize in a real app
    // ------------------------------------------------------------------
    glm::mat4 proj = glm::perspective(glm::radians(70.0f),
                                      1280.0f / 720.0f,
                                      0.1f, 1000.0f);

    // ------------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------------
    float lastTime = (float)glfwGetTime();
    while (win.isOpen()) {
        float now = (float)glfwGetTime();
        float dt  = now - lastTime;
        lastTime  = now;

        // Input + local movement
        input.dt = dt;
        input.update(win.handle, localPlayer, win.isCursorLocked());
        if (input.escapePressed) win.toggleCursor();

        // Send predicted position to server
        if (client.isConnected()) {
            client.sendInput(
                localPlayer.state.position.x,
                localPlayer.state.position.y,
                localPlayer.state.position.z,
                localPlayer.state.yaw,
                localPlayer.state.pitch
            );
        }

        // Pump incoming packets — updates client.getRemotePlayers()
        client.tick();

        for (auto& coord : client.getAndClearNewChunks()) {
            const auto& chunks = client.getChunks();
            auto it = chunks.find(coord);
            if (it != chunks.end())
                renderer.submitChunk(coord, it->second);
        }

        // Render
        glClearColor(0.53f, 0.81f, 0.98f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = localPlayer.cam.getViewMatrix();

        renderer.drawChunks(view, proj);

        // Draw remote players as cubes
        for (auto& [id, rp] : client.getRemotePlayers())
            renderer.drawCube(view, proj, rp.state.position, {0.9f, 0.2f, 0.2f});

        win.swapAndPoll();
    }

    // ------------------------------------------------------------------
    // Cleanup in reverse init order
    // ------------------------------------------------------------------
    // renderer.cleanup();
    client.disconnect();

    if (singleplayer) {
        localServer.stop();
    }

    // window.destroy();
    enet_deinitialize();
    return 0;
}