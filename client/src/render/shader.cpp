// render/shader.cpp
#include "render/shader.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>

static unsigned int compileStage(unsigned int type, const std::string& src) {
    unsigned int id = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(id, 1, &c, nullptr);
    glCompileShader(id);
    int ok;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(id, 512, nullptr, log);
        fprintf(stderr, "[loadShader] Compile error:\n%s\n", log);
        return 0;
    }
    return id;
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "[loadShader] Cannot open: %s\n", path.c_str()); return ""; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

unsigned int loadShader(const std::string& vertPath, const std::string& fragPath) {
    unsigned int vert = compileStage(GL_VERTEX_SHADER,   readFile(vertPath));
    unsigned int frag = compileStage(GL_FRAGMENT_SHADER, readFile(fragPath));
    if (!vert || !frag) return 0;

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    int ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        fprintf(stderr, "[loadShader] Link error:\n%s\n", log);
        return 0;
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}