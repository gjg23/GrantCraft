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

// client core
#include "client_core/local_server.hpp"
#include "client_core/client.hpp"

// renderer
#include "render/renderer.hpp"
#include "render/window.hpp"
#include "render/shader.hpp"
#include "render/texture.hpp"

// game core
#include "game_core/camera.hpp"
#include "game_core/input.hpp"
#include "game_core/remote_player.hpp"

// Debug
#include "client_core/command_options.hpp"

int main(int argc, char* argv[]) {
    // -----------------------------------------------------------------------------
    // Usage:
    // Singleplayer
    //   ./game_client
    //   ./game_client --gpu
    //   ./game_client --cpu
    //
    // Multiplayer
    //   ./game_client <host> <port>
    //   ./game_client <host> <port> --gpu
    //   ./game_client <host> <port> --cpu
    //
    // Benchmark
    //   ./game_client --bench
    //   ./game_client --bench --host <host> --port <port>
    //   ./game_client --bench --user <name>
    //   ./game_client --bench --bench-radius <radius>
    //
    // Debug
    //   ./game_client --debug
    //   ./game_client --debug --host <host> --port <port>
    //   ./game_client --debug --user <name>
    //
    // -----------------------------------------------------------------------------
    
    CommandLineOptions opt;

    if (!ArgumentParser::parse(argc, argv, opt))
        return 1;

    bool singleplayer = opt.singleplayer();

    if (singleplayer)
    {
        opt.remoteHost = "127.0.0.1";
        opt.remotePort = 7778;
    }

    if (!singleplayer && opt.remotePort == 0)
    {
        fprintf(stderr,
                "Usage: %s [host port]\n",
                argv[0]);
        return 1;
    }

    if (opt.isBench)
    {
        BenchArgs ba;

        ba.mode = opt.benchMode;
        ba.radius = (opt.radius > 0) ? opt.radius : 4;
        ba.genCPU = !opt.benchGPU;
        ba.genGPU = opt.benchGPU ||
                    (opt.mode == ServerMode::GPU);

        ba.port = singleplayer
                    ? 7779
                    : opt.remotePort;

        ba.players = opt.benchPlayers;
        ba.durationS = opt.benchDuration;
        ba.spawnRadius = opt.benchSpawnR;
        ba.stationaryS = opt.benchStationary;
        ba.gpuServer =
            (opt.mode == ServerMode::GPU) ||
            opt.benchGPU;

        ba.csvPath = opt.benchOutput;

        runBench(ba);
        return 0;
    }

    // Setup debugger if enabled (just extra terminal output)
    DebugOverlay debug;
    if (opt.isDebug) debug.init();

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
        if (!localServer.start(opt.remotePort, opt.mode)) {
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
    if (!win.init(1280, 720, "Masters Project")) {
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
    
    // ------------------------------------------------------------------
    // renderer
    // ------------------------------------------------------------------
    unsigned int skyShader   = loadShader("shaders/sky.vert",   "shaders/sky.frag");
    unsigned int blockShader = loadShader("shaders/basic.vert", "shaders/basic.frag");
    if (!skyShader || !blockShader) {
        fprintf(stderr, "Shader loading failed\n");
        exit(EXIT_FAILURE);
    }
    Renderer renderer;
    DayNightCycle dayNight;
    renderer.init(skyShader, blockShader, loadTexture("textures/atlas_2.png"));

    // ------------------------------------------------------------------
    // Connect to the server (local or remote)
    // ------------------------------------------------------------------
    printf("Connecting to %s:%u.\n", opt.remoteHost, opt.remotePort);
    Client client;
    if (!client.connect(opt.remoteHost, opt.remotePort, "Player1")) {
        fprintf(stderr, "Could not connect to server.\n");
        exit(EXIT_FAILURE);
    }

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

        auto tierDelta = client.updatePlayerChunk(
            localPlayer.state.position.x,
            localPlayer.state.position.y,
            localPlayer.state.position.z
        );

        // Apply movement-driven chunk transitions for already-cached chunks.

        // 1) Chunks that must become renderer-resident again
        for (const auto& coord : tierDelta.toLoadRenderer) {
            std::vector<BlockType> blocks;
            if (client.copyChunkBlocks(coord, blocks)) {
                renderer.submitChunk(coord, std::move(blocks));
                client.markRendererResident(coord);
            }
        }

        // 2) Chunks entering render distance: show
        for (const auto& coord : tierDelta.toShow) {
            renderer.setChunkVisible(coord, true);
        }

        // 3) Chunks leaving render distance but still inside simulation distance: hide only
        for (const auto& coord : tierDelta.toHide) {
            renderer.setChunkVisible(coord, false);
        }

        // 4) Chunks leaving simulation distance entirely: drop renderer-side storage
        for (const auto& coord : tierDelta.toDropRenderer) {
            renderer.removeChunk(coord);
            client.markRendererReleased(coord);
        }

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
        if (opt.isDebug) debug.update(client);

        // Newly arrived chunks from the server:
        for (auto& evt : client.drainNewChunks()) {
            if (evt.tier == ChunkTier::Render) {
                std::vector<BlockType> blocks;
                if (client.copyChunkBlocks(evt.coord, blocks)) {
                    renderer.submitChunk(evt.coord, std::move(blocks));
                    client.markRendererResident(evt.coord);
                    renderer.setChunkVisible(evt.coord, true);
                }
            }
        }

        // Render
        dayNight.update(dt);

        RenderContext ctx;
        ctx.proj = glm::perspective(glm::radians(70.0f), win.aspectRatio(), 0.1f, 1000.0f);
        ctx.view = localPlayer.cam.getViewMatrix();
        dayNight.fill(ctx);

        renderer.beginFrame();
        renderer.renderSky(ctx, localPlayer.cam.position);  // needs cam.position exposed
        renderer.renderWorld(ctx);

        // Remote players still use drawCube
        for (auto& [id, rp] : client.getRemotePlayers())
            renderer.drawCube(ctx.view, ctx.proj, rp.state.position, {0.9f, 0.2f, 0.2f});

        win.swapAndPoll();
    }

    // ------------------------------------------------------------------
    // Cleanup in reverse init order
    // ------------------------------------------------------------------
    renderer.cleanup();
    client.disconnect();
    if (singleplayer) localServer.stop();
    win.destroy();
    enet_deinitialize();
    return 0;
}
