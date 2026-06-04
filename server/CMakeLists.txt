# ----- Chunk System -----
add_library(chunk_core STATIC
    src/generation/chunk_system.cpp
    src/generation/terrain_gen.cpp
    src/generation/chunk_interest.cpp
)
target_include_directories(chunk_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(chunk_core PUBLIC
    shared_lib
)

# ----- Server lib -----
add_library(server_lib STATIC
    src/server.cpp
)
target_include_directories(server_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(server_lib PUBLIC
    shared_lib
    chunk_core
)

# ----- Server Executible -----
add_executable(game_server
    src/main.cpp
)
target_link_libraries(game_server PRIVATE
    server_lib
)