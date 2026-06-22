#version 330 core
// basic.vert
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aNormal; // packed normal index

out vec2 UV;
out float Diffuse;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;
uniform vec3 sunDir;      // normalized, points toward the sun
uniform vec3 sunColor;
uniform vec3 ambientColor;

// Decode face index -> normal
const vec3 normals[6] = vec3[6](
    vec3( 1, 0, 0),  // +X
    vec3(-1, 0, 0),  // -X
    vec3( 0, 1, 0),  // +Y
    vec3( 0,-1, 0),  // -Y
    vec3( 0, 0, 1),  // +Z
    vec3( 0, 0,-1)   // -Z
);

void main() {
    gl_Position = proj * view * model * vec4(aPos, 1.0);
    UV = aUV;

    vec3 normal = normals[int(aNormal)];
    float sun = max(dot(normal, sunDir), 0.0);
    Diffuse = sun;
}
