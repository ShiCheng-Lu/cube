#version 450

// Overlay lines (panel outlines), already in the view's world space

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(push_constant) uniform Push { mat4 viewProj; };

layout(location = 0) out vec3 outColor;

void main() {
    outColor = inColor;
    gl_Position = viewProj * vec4(inPosition, 1.0);
}
