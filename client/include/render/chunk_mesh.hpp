#pragma once
// ------------------------------------------------------------------
// client/include/render/chunk_mesh.hpp
// ------------------------------------------------------------------
#include <glad.h>
#include <vector>
#include "world/chunk.hpp"

struct ChunkMesh {
    GLuint vao       = 0;
    GLuint vbo       = 0;
    int    vertCount = 0;

    bool built = false;

    void build(const Chunk& chunk);
    void draw()    const;
    void cleanup();
};