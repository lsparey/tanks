#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    // Only this common prefix of the C++ FrameUBO is needed by vertices.
} frame;

layout(std430, set = 0, binding = 1) readonly buffer InstanceTransforms {
    mat4 transforms[];
} instanceData;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float unlit;
    float specularStrength;
    float heightBlend;
    float opacity;
    float reflectivity;
    float waveStrength;
    float bumpStrength;
    float isDynamicObject;
    float materialType;
    float isInstanced;
    vec4 tankSurface;
} pc;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 fragWorldPos;
// The model matrix's own local +X axis in world space -- constant across a
// single (rigid, non-skinned) draw instance, so every vertex just carries
// the same value out. Lets basic.frag reconstruct a true per-instance
// tangent for decal bump mapping (see PushConstants::bumpStrength) without
// needing a dedicated tangent vertex attribute.
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragModelPos;

void main() {
    mat4 model = pc.isInstanced > 0.5 ? instanceData.transforms[gl_InstanceIndex] : pc.model;
    vec4 worldPos = model * vec4(inPosition, 1.0);
    gl_Position = frame.proj * frame.view * worldPos;
    if (pc.materialType > 3.5 && pc.materialType < 4.5)
        gl_Position.z = gl_Position.w * 0.99999;

    // Assumes uniform scale (no non-uniform scaling applied to any mesh in
    // this prototype), so the model matrix itself is fine for normals --
    // no inverse-transpose needed.
    fragNormal = mat3(model) * inNormal;
    fragColor = inColor;
    fragUV = inUV;
    fragWorldPos = worldPos.xyz;
    fragModelPos = inPosition;
    // Running gear uses shared local meshes. UVs/edge masks rotate with each
    // part, while height-based dust is evaluated in the tank hull's frame.
    if (pc.isInstanced > 0.5 && pc.materialType > 4.5 && pc.materialType < 7.5)
        fragModelPos = vec3(pc.model * worldPos);
    fragTangent = mat3(model) * vec3(1.0, 0.0, 0.0);
}
