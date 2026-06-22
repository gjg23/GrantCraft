#version 330 core

out vec4 FragColor;

uniform mat4 invProjView;
uniform vec3 camPos;
uniform vec3 skyZenith;     // top color
uniform vec3 skyHorizon;    // horizon color
uniform vec3 sunDir;        // same sunDir as block shader
uniform float sunStrength;  // 0 at night, 1 at noon

void main() {
    vec2 ndc     = (gl_FragCoord.xy / vec2(800.0, 600.0)) * 2.0 - 1.0;
    vec4 worldDir = invProjView * vec4(ndc, 1.0, 1.0);
    vec3 dir      = normalize(worldDir.xyz / worldDir.w - camPos);

    // Sky gradient — zenith to horizon
    float t     = clamp(dir.y * 1.5 + 0.3, 0.0, 1.0);
    vec3 sky    = mix(skyHorizon, skyZenith, t);

    // Sun disc + glow
    float sunDot  = max(dot(dir, sunDir), 0.0);
    float sunDisc = smoothstep(0.997, 1.0, sunDot);          // hard disc
    float sunGlow = pow(sunDot, 16.0) * 0.4 * sunStrength;   // soft glow around it

    // Sunrise/sunset horizon glow — warm band near horizon in sun direction
    float horizonGlow = pow(max(dot(dir * vec3(1,0,1), sunDir * vec3(1,0,1)), 0.0), 6.0)
                        * (1.0 - abs(dir.y))   // only near horizon
                        * sunStrength;
    vec3 glowColor = mix(vec3(1.0, 0.4, 0.1), vec3(1.0, 0.8, 0.4), sunStrength);

    vec3 color = sky
               + glowColor * horizonGlow * 0.6
               + vec3(1.0, 0.95, 0.7) * sunGlow
               + vec3(1.0, 1.0, 0.9)  * sunDisc * sunStrength;

    FragColor = vec4(color, 1.0);
}