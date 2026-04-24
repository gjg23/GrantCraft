cmake_minimum_required(VERSION 3.16)
project(GrantCraft)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party/enet)
add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(client)