#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragWorldPos;

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
    float specularStrength;
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

    // Everything below is gated by specularStrength (0 for terrain/other
    // matte objects), so only opted-in draws (the tank) get these.
    vec3 viewDir = normalize(frame.cameraPos.xyz - fragWorldPos);

    // Blinn-Phong specular -- a lower exponent than a glossy/chrome look
    // would use gives a broader, softer highlight, reading as duller,
    // brushed metal rather than polished plastic.
    vec3 halfDir = normalize(toLight + viewDir);
    float specAngle = max(dot(normal, halfDir), 0.0);
    float specular = pow(specAngle, 20.0) * pc.specularStrength;

    // Fresnel/rim term: surfaces brighten at grazing view angles, a cheap
    // but very characteristic cue for metal.
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0) * pc.specularStrength;

    // Fake environment reflection: no real cubemap, just a two-tone
    // sky/ground gradient sampled by the reflection vector's vertical
    // component. Adds subtle view-dependent color variation across the
    // hull instead of one flat painted color, which reads as "reflective."
    vec3 reflectDir = reflect(-viewDir, normal);
    vec3 skyTint = vec3(0.55, 0.65, 0.78);
    vec3 groundTint = vec3(0.12, 0.11, 0.10);
    vec3 envColor = mix(groundTint, skyTint, clamp(reflectDir.y * 0.5 + 0.5, 0.0, 1.0));

    vec3 result = albedo * lighting + vec3(specular) + fresnel * vec3(0.9) +
                  envColor * pc.specularStrength * 0.35;
    outColor = vec4(result, 1.0);
}
