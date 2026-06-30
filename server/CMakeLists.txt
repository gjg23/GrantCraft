# ----- GPU terrain -----
add_library(gpu_terrain STATIC
    src/generation/gpu_terrain/gpu_launch.cu
    src/generation/gpu_terrain/kernel.cu
    # src/generation/gpu_terrain/terrain_params.cu
)
target_include_directories(gpu_terrain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(gpu_terrain PUBLIC
    CUDA::cudart
    shared_lib
)
set_target_properties(gpu_terrain PROPERTIES
    CUDA_SEPARABLE_COMPILATION OFF
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
)


# ----- CPU terrain -----
add_library(cpu_terrain STATIC
    src/generation/terrain_gen.cpp
    src/generation/terrain_detail.cpp
)
target_include_directories(cpu_terrain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cpu_terrain PUBLIC
    shared_lib
)

# ----- Chunk System -----
add_library(chunk_core STATIC
    src/generation/chunk_worker_pool.cpp
    src/generation/chunk_interest.cpp
    src/generation/chunk_registry.cpp
)
target_include_directories(chunk_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(chunk_core PUBLIC
    cpu_terrain
    gpu_terrain
    CUDA::cudart
)
target_link_libraries(chunk_core PRIVATE lz4) # compression for chunks
set_target_properties(chunk_core PROPERTIES
    CUDA_SEPARABLE_COMPILATION OFF
)


# ----- Server lib -----
add_library(server_lib STATIC
    src/server.cpp
    src/scheduler/task_pool.cpp
    src/scheduler/fifo_sched.cpp
    src/scheduler/adaptive_sched.cpp
    src/scheduler/priortity_sched.cpp
)
target_include_directories(server_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(server_lib PUBLIC
    chunk_core
)
set_target_properties(server_lib PROPERTIES
    CUDA_SEPARABLE_COMPILATION OFF
)


# ----- Server Executible -----
add_executable(game_server
    src/main.cpp
)
target_link_libraries(game_server PRIVATE
    server_lib
)