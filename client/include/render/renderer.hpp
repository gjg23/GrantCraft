#pragma once
// client/include/render/renderer.hpp
 
#include <glad.h>
#include <glm/glm.hpp>
 
#include "render/chunk_renderer.hpp"  // owns ChunkRenderer

#include <unordered_map>

// ------------------------------------------------------------------
// RenderContext - passed each frame to all render calls
// ------------------------------------------------------------------
struct RenderContext {
    glm::mat4 proj;
    glm::mat4 view;

    glm::vec3 sunDir;
    glm::vec3 sunColor;
    glm::vec3 ambientColor;

    glm::vec3 skyZenith;
    glm::vec3 skyHorizon;
};

// ------------------------------------------------------------------
// Cached uniform locations for each shader
// ------------------------------------------------------------------
struct BlockUniforms {
    GLint sunDir       = -1;
    GLint sunColor     = -1;
    GLint ambientColor = -1;
    GLint model        = -1;
    GLint view         = -1;
    GLint proj         = -1;
};

struct SkyUniforms {
    GLint invProjView = -1;
    GLint camPos      = -1;
    GLint skyZenith   = -1;
    GLint skyHorizon  = -1;
    GLint sunDir      = -1;
    GLint sunStrength = -1;
};

// ------------------------------------------------------------------
// DayNightCycle - drives lighting & sky colours over time
// ------------------------------------------------------------------
class DayNightCycle {
public:
    float timeOfDay = 0.0f; // start at morning
    float speed     = 0.0025f;

    void update(float dt);
    void fill(RenderContext& ctx);
};

// ------------------------------------------------------------------
// Renderer
// ------------------------------------------------------------------
class Renderer {
public:
    // atlas is a GL texture ID from loadTexture().
    void init(unsigned int skyShader, unsigned int blockShader, unsigned int atlas);
    ~Renderer() { cleanup(); }

    void beginFrame();
    void renderSky  (const RenderContext& ctx, const glm::vec3& camPos);
    void renderWorld(const RenderContext& ctx);

    // Old: just used for drawing the player cubes
    void drawCube(const glm::mat4& view,
                  const glm::mat4& proj,
                  const glm::vec3& worldPos,
                  const glm::vec3& colour);

    // Feed new chunk data in from the network (thread-safe).
    void submitChunk(const ChunkCoord& coord, std::vector<BlockType> blocks);

    // Hide/show without destroying mesh residency
    void setChunkVisible(const ChunkCoord& coord, bool visible);
 
    // Remove a chunk (main thread only).
    void removeChunk(const ChunkCoord& coord);
 
    // Block until background mesher drains (call once after initial load).
    void waitMeshIdle() { m_chunkRenderer.waitIdle(); }
 
    void cleanup();

    bool hasMesh(ChunkCoord coord) { return m_chunkRenderer.hasMesh(coord); }

    void setMeshEvictedCallback(std::function<void(const ChunkCoord&)> cb) {
        m_chunkRenderer.onMeshEvicted = std::move(cb);
    }
    bool hasChunkMesh(const ChunkCoord& c) const {
        return m_chunkRenderer.hasMesh(c);
    }

    void setPlayerChunk(const ChunkCoord& c) {
        m_chunkRenderer.setPlayerChunk(c);
    }

    void setRenderRadius(int r) { m_chunkRenderer.setRenderRadius(r); }

private:
    // Programs
    GLuint m_skyShader   = 0;
    GLuint m_blockShader = 0;

    // Cached uniform locations
    SkyUniforms   m_skyLocs;
    BlockUniforms m_blockLocs;

    // Sky fullscreen triangle
    GLuint m_skyVAO = 0;

    // Texture atlas
    GLuint m_atlasTex = 0;

    // Chunk meshing + GPU storage
    ChunkRenderer m_chunkRenderer;

    // Cube geometry for drawCube()
    GLuint m_cubeVAO    = 0;
    GLuint m_cubeVBO    = 0;
    GLuint m_cubeShader = 0;   // simple MVP+colour shader, compiled inline
    GLint  m_uMVP       = -1;
    GLint  m_uColour    = -1;

    GLuint compileCubeShader(); // compiles the hardcoded MVP shader
    GLuint compileShader(GLenum type, const char* src);

    // no double clean
    bool m_cleaned = false;
};