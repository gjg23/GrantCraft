// client/src/render/chunk_mesh.cpp
#include "render/chunk_mesh.hpp"
#include "world/block.hpp"
#include <glm/glm.hpp>

#include <iostream>

// One quad = 6 floats per vertex (x,y,z, nx,ny,nz), 6 verts per face
static void pushFace(std::vector<float>& verts, float x, float y, float z, int axis, int dir) {
    // 4 corners of the face, wound for front-face culling
    // axis 0=X, 1=Y, 2=Z; dir +1 or -1
    static const float FACES[6][6][3] = {
        // +X
        {{1,0,0},{1,1,0},{1,1,1}, {1,0,0},{1,1,1},{1,0,1}},
        // -X
        {{0,0,1},{0,1,1},{0,1,0}, {0,0,1},{0,1,0},{0,0,0}},
        // +Y
        {{0,1,0},{0,1,1},{1,1,1}, {0,1,0},{1,1,1},{1,1,0}},
        // -Y
        {{0,0,1},{0,0,0},{1,0,0}, {0,0,1},{1,0,0},{1,0,1}},
        // +Z
        {{1,0,1},{1,1,1},{0,1,1}, {1,0,1},{0,1,1},{0,0,1}},
        // -Z
        {{0,0,0},{0,1,0},{1,1,0}, {0,0,0},{1,1,0},{1,0,0}},
    };
    int faceIdx = axis * 2 + (dir > 0 ? 0 : 1);
    for (int v = 0; v < 6; ++v) {
        verts.push_back(x + FACES[faceIdx][v][0]);
        verts.push_back(y + FACES[faceIdx][v][1]);
        verts.push_back(z + FACES[faceIdx][v][2]);
    }
}

void ChunkMesh::build(const Chunk& chunk) {
    std::vector<float> verts;
    verts.reserve(1024);

    glm::ivec3 origin = chunkOrigin(chunk.coord);

    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    for (int ly = 0; ly < CHUNK_SIZE; ++ly)
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        if (chunk.getBlockType(lx, ly, lz) == BlockType::Air) continue;

        float wx = (float)(origin.x + lx);
        float wy = (float)(origin.y + ly);
        float wz = (float)(origin.z + lz);

        // Emit a face only when the neighbour is air (visible face)
        // Clamping keeps us in-bounds; border faces always emit
        auto air = [&](int nx, int ny, int nz) {
            if (nx < 0 || nx >= CHUNK_SIZE ||
                ny < 0 || ny >= CHUNK_SIZE ||
                nz < 0 || nz >= CHUNK_SIZE) return true;
            return chunk.getBlockType(nx, ny, nz) == BlockType::Air;
        };

        if (air(lx+1, ly,   lz  )) pushFace(verts, wx, wy, wz, 0, +1);
        if (air(lx-1, ly,   lz  )) pushFace(verts, wx, wy, wz, 0, -1);
        if (air(lx,   ly+1, lz  )) pushFace(verts, wx, wy, wz, 1, +1);
        if (air(lx,   ly-1, lz  )) pushFace(verts, wx, wy, wz, 1, -1);
        if (air(lx,   ly,   lz+1)) pushFace(verts, wx, wy, wz, 2, +1);
        if (air(lx,   ly,   lz-1)) pushFace(verts, wx, wy, wz, 2, -1);
    }

    vertCount = (int)verts.size() / 3;
    if (vertCount == 0) { built = true; return; }

    if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    built = true;
}

void ChunkMesh::draw() const {
    if (!built || vertCount == 0) return;
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, vertCount);
    glBindVertexArray(0);
}

void ChunkMesh::cleanup() {
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo);       vbo = 0; }
}