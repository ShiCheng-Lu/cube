#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

// geometry-to-world matrix of every bone, for this frame
layout(set = 0, binding = 0) readonly buffer Bones { mat4 bones[]; };

layout(push_constant) uniform Push { mat4 viewProj; };

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outColor;

void main() {
    mat4 skin = inWeights.x * bones[inJoints.x]
              + inWeights.y * bones[inJoints.y]
              + inWeights.z * bones[inJoints.z]
              + inWeights.w * bones[inJoints.w];

    outNormal = mat3(skin) * inNormal;
    outColor = inColor;
    gl_Position = viewProj * (skin * vec4(inPosition, 1.0));
}
