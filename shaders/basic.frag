#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 lightDir;   // direction the light travels, xyz
    vec4 cameraPos;
} frame;

layout(set = 1, binding = 0) uniform sampler2D materialTex;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float unlit;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 texColor = texture(materialTex, fragUV).rgb;
    vec3 albedo = fragColor * texColor;

    if (pc.unlit > 0.5) {
        outColor = vec4(albedo, 1.0);
        return;
    }

    vec3 normal = normalize(fragNormal);
    vec3 toLight = normalize(-frame.lightDir.xyz);
    float diffuse = max(dot(normal, toLight), 0.0);
    // Small ambient/fill term so faces pointing away from the light read as
    // dim rather than pure black.
    float lighting = 0.2 + 0.8 * diffuse;
    outColor = vec4(albedo * lighting, 1.0);
}
