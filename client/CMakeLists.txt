# ===== Renderer ===================================
add_library(renderer
    src/render/renderer.cpp
    src/render/window.cpp
    src/render/chunk_mesh.cpp
    src/render/shader.cpp
    src/render/texture.cpp
    src/render/chunk_mesher.cpp
    src/render/chunk_renderer.cpp
)
target_include_directories(renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# GLFW
if(TARGET glfw3::glfw3)
    set(GLFW_TARGET glfw3::glfw3)
else()
    set(GLFW_TARGET glfw)
endif()

target_link_libraries(renderer PRIVATE stb_image)

target_link_libraries(renderer PUBLIC
    glad
    ${GLFW_TARGET}
    OpenGL::GL
    glm::glm          # glm is header-only but has an imported target; prefer it
    shared_lib        # renderer needs Chunk/block data to build meshes
    server_lib
)

# ===== Game Core ===================================
add_library(game_core
    src/game_core/input.cpp
    src/game_core/player_state.cpp
    src/game_core/camera.cpp
)
target_include_directories(game_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# link local server and shared lib
target_link_libraries(game_core PRIVATE
    shared_lib
    glad
    ${GLFW_TARGET}
)

# ===== Client Core ===================================
add_library(client_core
    src/client_core/client.cpp
    src/client_core/local_server.cpp
    src/bench/bench_mode.cpp
    src/bench/debug_mode.cpp
)
target_include_directories(client_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# link local server and shared lib
target_link_libraries(client_core PRIVATE
    shared_lib
    server_lib
    renderer
    glad
    enet
    ${GLFW_TARGET}
)

# ===== Game Client Executable ===================================
add_executable(game_client
    src/main.cpp
)

target_link_libraries(game_client PRIVATE
    client_core
    game_core
    enet
    ${GLFW_TARGET}
    glad
    server_lib
)
