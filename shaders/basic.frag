#version 330 core
// basic.frag
in vec2 UV;
in float Diffuse;

out vec4 FragColor;

uniform sampler2D atlas;
uniform vec3 sunColor;
uniform vec3 ambientColor;

void main() {
    vec4 texColor = texture(atlas, UV);
    if (texColor.a < 0.1) discard;

    vec3 light = ambientColor + sunColor * Diffuse;
    FragColor = vec4(texColor.rgb * light, texColor.a);
}