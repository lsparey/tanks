#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 prevViewProj;
    vec4 lightDir;
    vec4 cameraPos;
    vec4 prevCameraPos;
    vec4 windTime;
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

layout(location = 6) out vec3 fragPrevWorldPos;
layout(location = 7) out vec3 fragRayWorldPos;
layout(location = 8) out vec3 fragRayNormal;

// One wind vector per tree: bark and every canopy LOD use the same root.
// No phase varies across the canopy surface. Integer harmonics make the
// CPU's 128-second time wrap seamless, independently of rendering FPS.
vec3 treeBend(mat4 model, float seconds) {
    const float omega = 6.28318530718 / 128.0;
    float phase = dot(model[3].xz, vec2(0.08, 0.05));
    float along = 0.007 + 0.009 * sin(seconds * omega * 13.0 + phase)
                         + 0.003 * sin(seconds * omega * 5.0 + phase * 1.7);
    float across = 0.004 * sin(seconds * omega * 17.0 + phase + 1.2);
    vec2 wind = vec2(0.8, 0.6) * along + vec2(-0.6, 0.8) * across;
    // Tree instances have uniform scale; express the shared world wind
    // in local axes so the bend follows each trunk's orientation.
    mat3 rotation = mat3(model) / length(model[1].xyz);
    vec3 bend = transpose(rotation) * vec3(wind.x, 0.0, wind.y);
    bend.y = 0.0;
    return bend;
}

void main() {
    mat4 model = pc.isInstanced > 0.5 ? instanceData.transforms[gl_InstanceIndex] : pc.model;
    vec4 worldPos = model * vec4(inPosition, 1.0);
    vec4 renderPos = worldPos;
    vec4 previousPos = worldPos;
    vec3 renderNormal = inNormal;
    bool foliage = pc.materialType > 1.5 && pc.materialType < 2.5;
    bool bark = pc.materialType > 9.5 && pc.materialType < 10.5;
    if (foliage || bark) {
        // Quadratic bending anchors both root position and root slope.
        // The lower trunk stays stiff; displacement grows smoothly toward
        // the crown instead of saturating into a sideways translation.
        float height = max(inPosition.y, 0.0);
        vec3 bend = treeBend(model, frame.windTime.x);
        vec3 previousBend = treeBend(model, frame.windTime.y);
        renderPos = model * vec4(inPosition + bend * height * height, 1.0);
        previousPos = model * vec4(inPosition + previousBend * height * height, 1.0);
        // Inverse transpose of the bend Jacobian, before the rigid model
        // transform. This keeps illumination attached to the bent surface.
        renderNormal.y -= 2.0 * height * dot(bend, inNormal);
    }
    gl_Position = frame.proj * frame.view * renderPos;
    if (pc.materialType > 3.5 && pc.materialType < 4.5)
        gl_Position.z = gl_Position.w * 0.99999;

    fragNormal = mat3(model) * renderNormal;
    fragColor = inColor;
    fragUV = inUV;
    fragWorldPos = renderPos.xyz;
    fragPrevWorldPos = previousPos.xyz;
    // Ray queries still use the static acceleration geometry. Keep these
    // separate from the displaced lighting and temporal-history positions.
    fragRayWorldPos = worldPos.xyz;
    fragRayNormal = mat3(model) * inNormal;
    fragModelPos = inPosition;
    if (pc.isInstanced > 0.5 && pc.materialType > 4.5 && pc.materialType < 7.5)
        fragModelPos = vec3(pc.model * worldPos);
    // Bending depends only on local height, so the local X derivative
    // (used as the tangent for decals) is unchanged.
    fragTangent = mat3(model) * vec3(1.0, 0.0, 0.0);
}
