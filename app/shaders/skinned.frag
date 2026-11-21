#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    // faces are drawn from both sides, so light whichever side faces the camera
    vec3 n = normalize(gl_FrontFacing ? inNormal : -inNormal);
    vec3 light = normalize(vec3(0.4, 1.0, 0.6));
    float diffuse = max(dot(n, light), 0.0);
    float ambient = mix(0.15, 0.35, n.y * 0.5 + 0.5);
    outColor = vec4(inColor * (ambient + 0.8 * diffuse), 1.0);
}
