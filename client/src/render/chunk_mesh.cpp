// render/chunk_mesh.cpp
#include "render/chunk_mesh.hpp"
#include "world/block.hpp"
#include <glm/glm.hpp>

// Vertex format: x,y,z, u,v, faceIdx  (6 floats per vertex)
// Each face = 4 verts + 6 indices (2 tris)

static const float TILEV = 1.0f / 19.0f; // match your old atlas row count

static void addFace(std::vector<float>& verts, std::vector<unsigned int>& idxs,
                    float x0, float y0, float z0,
                    float x1, float y1, float z1,
                    float x2, float y2, float z2,
                    float x3, float y3, float z3,
                    float u0, float v0,
                    float u1, float v1,
                    float u2, float v2,
                    float u3, float v3,
                    float faceIdx)
{
    unsigned int base = (unsigned int)(verts.size() / 6);
    verts.insert(verts.end(), {
        x0,y0,z0, u0,v0, faceIdx,
        x1,y1,z1, u1,v1, faceIdx,
        x2,y2,z2, u2,v2, faceIdx,
        x3,y3,z3, u3,v3, faceIdx,
    });
    idxs.insert(idxs.end(), {base, base+1, base+2, base, base+2, base+3});
}

void ChunkMesh::build(const Chunk& chunk) {
    std::vector<float>        verts;
    std::vector<unsigned int> idxs;
    verts.reserve(4096);
    idxs.reserve(2048);

    glm::ivec3 origin = chunkOrigin(chunk.coord);

    auto getBlock = [&](int lx, int ly, int lz) -> BlockType {
        if (lx < 0 || lx >= CHUNK_SIZE ||
            ly < 0 || ly >= CHUNK_SIZE ||
            lz < 0 || lz >= CHUNK_SIZE) return BlockType::Air;
        return chunk.getBlockType(lx, ly, lz);
    };

    auto visible = [&](int lx, int ly, int lz, int dx, int dy, int dz) {
        return getBlock(lx+dx, ly+dy, lz+dz) == BlockType::Air;
    };

    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    for (int ly = 0; ly < CHUNK_SIZE; ++ly)
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        BlockType b = chunk.getBlockType(lx, ly, lz);
        if (b == BlockType::Air) continue;

        float fx = (float)(origin.x + lx);
        float fy = (float)(origin.y + ly);
        float fz = (float)(origin.z + lz);

        int   row  = getTextureRow(b);   // your existing function
        float vTop = row       * TILEV;
        float vBot = (row + 1) * TILEV;

        // +X
        if (visible(lx,ly,lz, 1,0,0))
            addFace(verts,idxs,
                fx+1,fy,  fz,   fx+1,fy+1,fz,   fx+1,fy+1,fz+1, fx+1,fy,  fz+1,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 0.0f);
        // -X
        if (visible(lx,ly,lz,-1,0,0))
            addFace(verts,idxs,
                fx,fy,  fz+1, fx,fy+1,fz+1, fx,fy+1,fz,   fx,fy,  fz,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 1.0f);
        // +Y
        if (visible(lx,ly,lz, 0,1,0))
            addFace(verts,idxs,
                fx,  fy+1,fz,   fx,  fy+1,fz+1, fx+1,fy+1,fz+1, fx+1,fy+1,fz,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 2.0f);
        // -Y
        if (visible(lx,ly,lz, 0,-1,0))
            addFace(verts,idxs,
                fx,  fy,fz+1, fx,  fy,fz,   fx+1,fy,fz,   fx+1,fy,fz+1,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 3.0f);
        // +Z
        if (visible(lx,ly,lz, 0,0,1))
            addFace(verts,idxs,
                fx+1,fy,  fz+1, fx+1,fy+1,fz+1, fx,  fy+1,fz+1, fx,  fy,  fz+1,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 4.0f);
        // -Z
        if (visible(lx,ly,lz, 0,0,-1))
            addFace(verts,idxs,
                fx,  fy,  fz,   fx,  fy+1,fz,   fx+1,fy+1,fz,   fx+1,fy,  fz,
                0,vBot, 0,vTop, 1,vTop, 1,vBot, 5.0f);
    }

    indexCount = (int)idxs.size();
    if (indexCount == 0) { built = true; return; }

    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
    }

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxs.size() * sizeof(unsigned int), idxs.data(), GL_STATIC_DRAW);

    constexpr int STRIDE = 6 * sizeof(float);
    // location 0: position (xyz)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STRIDE, (void*)0);
    glEnableVertexAttribArray(0);
    // location 1: uv
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // location 2: faceIdx (used in shader for normal + lighting)
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    built = true;
}

void ChunkMesh::draw() const {
    if (!built || indexCount == 0) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void ChunkMesh::cleanup() {
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo);       vbo = 0; }
    if (ebo) { glDeleteBuffers(1, &ebo);       ebo = 0; }
}