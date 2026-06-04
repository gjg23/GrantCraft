# Shared library used for server and client
add_library(shared_lib STATIC
    src/chunk.cpp
)

# Shared is public for client and server
target_include_directories(shared_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# server and client need ENet types
target_link_libraries(shared_lib PUBLIC 
    enet
)