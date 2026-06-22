#pragma once
// render/texture.hpp

#include <glad.h>
#include <string>

// Loads an image from disk and uploads it as a GL texture.
// Returns the GL texture ID, or a 1x1 white fallback on failure.
unsigned int loadTexture(const std::string& path);