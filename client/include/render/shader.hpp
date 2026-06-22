#pragma once
// render/shader.hpp

#include <glad.h>
#include <string>

// Loads vert/frag GLSL from file paths and links them.
// Returns the GL program ID, or 0 on failure.
unsigned int loadShader(const std::string& vertPath, const std::string& fragPath);