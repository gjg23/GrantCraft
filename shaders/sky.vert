#version 330 core

// Fullscreen triangle trick — no VBO needed
// gl_VertexID 0,1,2 generates a triangle that covers the screen
out vec2 TexCoords;

void main() {
    vec2 pos = vec2((gl_VertexID & 1) << 2, (gl_VertexID & 2) << 1) - 1.0;
    gl_Position = vec4(pos, 0.999, 1.0); // 0.999 = far depth
    TexCoords = pos;
}