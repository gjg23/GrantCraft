cmake_minimum_required(VERSION 3.20)
project(GrantCraft)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----- ENet for networking -----
add_subdirectory(third_party/enet)
target_include_directories(enet PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/enet/include
)

# ----- OPenGL for client rendering -----
find_package(OpenGL REQUIRED)

# GLFW - prefer system package; falls back to source if not found
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "System GLFW not found - building from submodule")
    # Disable GLFW's own tests/examples so they don't bloat your build
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/glfw)
endif()

# GLM
find_package(glm REQUIRED)

# GLAD
add_library(glad STATIC third_party/glad/src/glad.c)
target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR}/third_party/glad/include)

# STB Image - header only
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party/stb_image
)

# ----- Subdirectories -----
add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(client)