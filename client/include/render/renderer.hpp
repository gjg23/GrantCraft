#pragma once
// ------------------------------------------------------------------
// renderer.h
// Minimal OpenGL renderer for the initial prototype.
// Draws a single coloured cube at a given position.
// Replace / extend with your full chunk renderer later.
// ------------------------------------------------------------------

#include <glad.h>
#include <glm/glm.hpp>
#include <world/chunk.hpp>
#include "render/chunk_mesh.hpp"
#include <unordered_map>

class Renderer {
public:
    // Upload geometry to the GPU - call once after GL context is ready
    bool init();

    // Draw a cube at worldPos with a flat colour
    void drawCube(const glm::mat4& view,
                  const glm::mat4& proj,
                  const glm::vec3& worldPos,
                  const glm::vec3& colour);

    // Placeholder: draws a wireframe outline of the chunk boundary.
    // Replace the body with a real mesh draw call once ChunkMesh exists.
    void drawChunk(const glm::mat4& view,
                   const glm::mat4& proj,
                   const ChunkCoord& coord);

    void cleanup();

    void submitChunk(const ChunkCoord& coord, const Chunk& chunk);
    void drawChunks (const glm::mat4& view, const glm::mat4& proj);

private:
    GLuint m_vao    = 0;
    GLuint m_vbo    = 0;
    GLuint m_shader = 0;

    GLint m_uMVP    = -1;
    GLint m_uColour = -1;

    // Compile a GLSL shader stage and return its ID
    GLuint compileShader(GLenum type, const char* src);

    std::unordered_map<ChunkCoord, ChunkMesh, ChunkCoordHash> m_meshes;
};