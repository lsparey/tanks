#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 lightDir;   // xyz, w unused
    vec4 cameraPos;  // xyz, w unused
} frame;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float unlit;
    float specularStrength;
} pc;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 fragWorldPos;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = frame.proj * frame.view * worldPos;

    // Assumes uniform scale (no non-uniform scaling applied to any mesh in
    // this prototype), so the model matrix itself is fine for normals --
    // no inverse-transpose needed.
    fragNormal = mat3(pc.model) * inNormal;
    fragColor = inColor;
    fragUV = inUV;
    fragWorldPos = worldPos.xyz;
}
