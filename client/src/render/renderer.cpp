// render/renderer.cpp
#include "render/renderer.hpp"
#include "render/chunk_mesh.hpp"

#include <cstdio>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

// -------------------------------------------------------
// Inline shaders used only for drawCube() (debug player cubes)
// -------------------------------------------------------
static const char* CUBE_VERT = R"glsl(
    #version 330 core
    layout(location = 0) in vec3 aPos;
    uniform mat4 uMVP;
    void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)glsl";

static const char* CUBE_FRAG = R"glsl(
    #version 330 core
    out vec4 FragColor;
    uniform vec3 uColour;
    void main() { FragColor = vec4(uColour, 1.0); }
)glsl";

static const float CUBE_VERTS[] = {
    // Back face
    0,0,0, 1,0,0, 1,1,0,  0,0,0, 1,1,0, 0,1,0,
    // Front face
    0,0,1, 1,1,1, 1,0,1,  0,0,1, 0,1,1, 1,1,1,
    // Left face
    0,0,0, 0,1,1, 0,0,1,  0,0,0, 0,1,0, 0,1,1,
    // Right face
    1,0,0, 1,0,1, 1,1,1,  1,0,0, 1,1,1, 1,1,0,
    // Bottom face
    0,0,0, 0,0,1, 1,0,1,  0,0,0, 1,0,1, 1,0,0,
    // Top face
    0,1,0, 1,1,1, 0,1,1,  0,1,0, 1,1,0, 1,1,1,
};


// ==========================================
// DayNightCycle
// ==========================================

void DayNightCycle::update(float dt) {
    timeOfDay += speed * dt;
    if (timeOfDay > 1.0f) timeOfDay -= 1.0f;
}

void DayNightCycle::fill(RenderContext& ctx) {
    float angle = timeOfDay * glm::two_pi<float>();

    ctx.sunDir = glm::normalize(glm::vec3(0.0f, sinf(angle), cosf(angle)));
    float sunStrength = glm::clamp(ctx.sunDir.y + 0.1f, 0.0f, 1.0f);

    float horizonBlend = 1.0f - glm::clamp(ctx.sunDir.y * 4.0f, 0.0f, 1.0f);
    glm::vec3 noonColor    = {1.0f,  0.95f, 0.8f};
    glm::vec3 sunriseColor = {1.0f,  0.5f,  0.2f};
    ctx.sunColor = glm::mix(noonColor, sunriseColor, horizonBlend) * sunStrength;

    float nightBlend = glm::clamp(-ctx.sunDir.y * 2.0f, 0.0f, 1.0f);
    glm::vec3 dayAmbient   = {0.35f, 0.45f, 0.6f};
    glm::vec3 nightAmbient = {0.12f, 0.12f, 0.2f};
    ctx.ambientColor = glm::mix(dayAmbient, nightAmbient, nightBlend);

    ctx.skyZenith = glm::mix(
        glm::vec3(0.15f, 0.4f,  0.9f),
        glm::vec3(0.01f, 0.01f, 0.08f),
        nightBlend);

    ctx.skyHorizon = glm::mix(
        glm::vec3(0.6f,  0.75f, 1.0f),
        glm::vec3(0.05f, 0.05f, 0.15f),
        nightBlend);

    ctx.skyHorizon = glm::mix(
        ctx.skyHorizon,
        glm::vec3(1.0f, 0.45f, 0.15f),
        horizonBlend * sunStrength);
}


// ==========================================
// Renderer
// ==========================================

void Renderer::init(unsigned int skyShader, unsigned int blockShader, unsigned int atlas) {
    m_skyShader   = skyShader;
    m_blockShader = blockShader;
    m_atlasTex    = atlas;

    // --- Sky fullscreen-triangle VAO (no vertex data needed) ---
    glGenVertexArrays(1, &m_skyVAO);

    // Cache sky uniform locations
    m_skyLocs.invProjView = glGetUniformLocation(m_skyShader, "invProjView");
    m_skyLocs.camPos      = glGetUniformLocation(m_skyShader, "camPos");
    m_skyLocs.skyZenith   = glGetUniformLocation(m_skyShader, "skyZenith");
    m_skyLocs.skyHorizon  = glGetUniformLocation(m_skyShader, "skyHorizon");
    m_skyLocs.sunDir      = glGetUniformLocation(m_skyShader, "sunDir");
    m_skyLocs.sunStrength = glGetUniformLocation(m_skyShader, "sunStrength");

    // Cache block uniform locations
    m_blockLocs.sunDir       = glGetUniformLocation(m_blockShader, "sunDir");
    m_blockLocs.sunColor     = glGetUniformLocation(m_blockShader, "sunColor");
    m_blockLocs.ambientColor = glGetUniformLocation(m_blockShader, "ambientColor");
    m_blockLocs.model        = glGetUniformLocation(m_blockShader, "model");
    m_blockLocs.view         = glGetUniformLocation(m_blockShader, "view");
    m_blockLocs.proj         = glGetUniformLocation(m_blockShader, "proj");

    // Bind atlas texture to slot 0 once
    glUseProgram(m_blockShader);
    glUniform1i(glGetUniformLocation(m_blockShader, "atlas"), 0);

    // --- Cube geometry for drawCube() ---
    m_cubeShader = compileCubeShader();
    m_uMVP    = glGetUniformLocation(m_cubeShader, "uMVP");
    m_uColour = glGetUniformLocation(m_cubeShader, "uColour");

    glGenVertexArrays(1, &m_cubeVAO);
    glGenBuffers(1, &m_cubeVBO);
    glBindVertexArray(m_cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTS), CUBE_VERTS, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void Renderer::beginFrame() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::renderSky(const RenderContext& ctx, const glm::vec3& camPos) {
    glDepthMask(GL_FALSE);

    glm::mat4 invProjView = glm::inverse(ctx.proj * ctx.view);
    float sunStrength = glm::clamp(ctx.sunDir.y + 0.1f, 0.0f, 1.0f);

    glUseProgram(m_skyShader);
    glUniformMatrix4fv(m_skyLocs.invProjView, 1, GL_FALSE, glm::value_ptr(invProjView));
    glUniform3fv(m_skyLocs.camPos,      1, glm::value_ptr(camPos));
    glUniform3fv(m_skyLocs.skyZenith,   1, glm::value_ptr(ctx.skyZenith));
    glUniform3fv(m_skyLocs.skyHorizon,  1, glm::value_ptr(ctx.skyHorizon));
    glUniform3fv(m_skyLocs.sunDir,      1, glm::value_ptr(ctx.sunDir));
    glUniform1f (m_skyLocs.sunStrength, sunStrength);

    glBindVertexArray(m_skyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3); // fullscreen triangle
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
}

void Renderer::renderWorld(const RenderContext& ctx,
                           const glm::mat4& view,
                           const glm::mat4& proj) {
    glUseProgram(m_blockShader);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlasTex);

    glm::mat4 model(1.0f);
    glUniform3fv(m_blockLocs.sunDir,       1, glm::value_ptr(ctx.sunDir));
    glUniform3fv(m_blockLocs.sunColor,     1, glm::value_ptr(ctx.sunColor));
    glUniform3fv(m_blockLocs.ambientColor, 1, glm::value_ptr(ctx.ambientColor));
    glUniformMatrix4fv(m_blockLocs.model, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(m_blockLocs.view,  1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(m_blockLocs.proj,  1, GL_FALSE, glm::value_ptr(proj));

    for (auto& [coord, mesh] : m_meshes)
        mesh.draw();
}

void Renderer::drawCube(const glm::mat4& view,
                        const glm::mat4& proj,
                        const glm::vec3& worldPos,
                        const glm::vec3& colour) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), worldPos);
    glm::mat4 mvp   = proj * view * model;

    glUseProgram(m_cubeShader);
    glUniformMatrix4fv(m_uMVP,    1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3fv      (m_uColour, 1,           glm::value_ptr(colour));

    glBindVertexArray(m_cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void Renderer::submitChunk(const ChunkCoord& coord, const Chunk& chunk) {
    m_meshes[coord].build(chunk);
}

void Renderer::cleanup() {
    glDeleteVertexArrays(1, &m_skyVAO);
    glDeleteVertexArrays(1, &m_cubeVAO);
    glDeleteBuffers(1, &m_cubeVBO);
    glDeleteProgram(m_cubeShader);
}


// ==================================================================
// Private helpers
// ==================================================================

GLuint Renderer::compileCubeShader() {
    GLuint vert = compileShader(GL_VERTEX_SHADER,   CUBE_VERT);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, CUBE_FRAG);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        fprintf(stderr, "[Renderer] Cube shader link error: %s\n", log);
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
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