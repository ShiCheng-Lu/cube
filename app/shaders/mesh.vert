#version 450

// Per-display pass: vertices are already skinned into world space (skin.comp)

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(push_constant) uniform Push { mat4 viewProj; };

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outColor;

void main() {
    outNormal = inNormal;
    outColor = inColor;
    gl_Position = viewProj * vec4(inPosition, 1.0);
}
