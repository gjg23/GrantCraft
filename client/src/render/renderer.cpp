// renderer.cpp
#include "render/renderer.hpp"
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// ------------------------------------------------------------------
// Minimal vertex shader - transforms position with MVP matrix
// ------------------------------------------------------------------
static const char* VERT_SRC = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uMVP;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)glsl";

// ------------------------------------------------------------------
// Minimal fragment shader - outputs a uniform colour
// ------------------------------------------------------------------
static const char* FRAG_SRC = R"glsl(
#version 330 core
out vec4 FragColor;

uniform vec3 uColour;

void main() {
    FragColor = vec4(uColour, 1.0);
}
)glsl";

// ------------------------------------------------------------------
// Unit cube vertices (36 verts, two triangles per face, 6 faces)
// Each vertex is just XYZ - no normals or UVs needed for a colour cube
// ------------------------------------------------------------------
static const float CUBE_VERTS[] = {
    // Back face
    0,0,0,  1,0,0,  1,1,0,   0,0,0,  1,1,0,  0,1,0,
    // Front face
    0,0,1,  1,1,1,  1,0,1,   0,0,1,  0,1,1,  1,1,1,
    // Left face
    0,0,0,  0,1,1,  0,0,1,   0,0,0,  0,1,0,  0,1,1,
    // Right face
    1,0,0,  1,0,1,  1,1,1,   1,0,0,  1,1,1,  1,1,0,
    // Bottom face
    0,0,0,  0,0,1,  1,0,1,   0,0,0,  1,0,1,  1,0,0,
    // Top face
    0,1,0,  1,1,1,  0,1,1,   0,1,0,  1,1,0,  1,1,1,
};

bool Renderer::init() {
    // --- Compile and link shader program ---
    GLuint vert = compileShader(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, FRAG_SRC);
    if (!vert || !frag) return false;

    m_shader = glCreateProgram();
    glAttachShader(m_shader, vert);
    glAttachShader(m_shader, frag);
    glLinkProgram(m_shader);

    m_uMVP    = glGetUniformLocation(m_shader, "uMVP");
    m_uColour = glGetUniformLocation(m_shader, "uColour");

    // Check link status
    GLint ok = 0;
    glGetProgramiv(m_shader, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(m_shader, 512, nullptr, log);
        fprintf(stderr, "[Renderer] Shader link error: %s\n", log);
        return false;
    }

    // Shaders are now in the program; individual objects not needed
    glDeleteShader(vert);
    glDeleteShader(frag);

    // --- Upload cube geometry ---
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTS), CUBE_VERTS, GL_STATIC_DRAW);

    // Attribute 0: position (3 floats, tightly packed)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    return true;
}

// Used just to draw players as cubes
void Renderer::drawCube(const glm::mat4& view,
                        const glm::mat4& proj,
                        const glm::vec3& worldPos,
                        const glm::vec3& colour) {
    // Build model matrix: just a translation to worldPos
    glm::mat4 model = glm::translate(glm::mat4(1.0f), worldPos);
    glm::mat4 mvp   = proj * view * model;

    glUseProgram(m_shader);
    glUniformMatrix4fv(glGetUniformLocation(m_shader, "uMVP"),
                       1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3fv(glGetUniformLocation(m_shader, "uColour"),
                 1, glm::value_ptr(colour));

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 36);   // 36 vertices = 12 triangles = 6 faces
    glBindVertexArray(0);
}

void Renderer::cleanup() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteProgram(m_shader);
}

GLuint Renderer::compileShader(GLenum type, const char* src) {
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(id, 512, nullptr, log);
        fprintf(stderr, "[Renderer] Shader compile error: %s\n", log);
        return 0;
    }
    return id;
}

// renderer.cpp additions
void Renderer::submitChunk(const ChunkCoord& coord, const Chunk& chunk) {
    m_meshes[coord].build(chunk);
}

void Renderer::drawChunks(const glm::mat4& view, const glm::mat4& proj) {
    // Identity model — positions are baked into vertex data during build()
    glm::mat4 mvp = proj * view;

    glUseProgram(m_shader);
    glUniformMatrix4fv(m_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3f(m_uColour, 0.4f, 0.7f, 0.3f);

    for (auto& [coord, mesh] : m_meshes)
        mesh.draw();
}