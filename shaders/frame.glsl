// Shared std140 layout; matches Pipeline::FrameUBO.
#define MAX_DYNAMIC_LIGHTS 4
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 prevViewProj;
    vec4 lightDir;   // direction the light travels, xyz
    vec4 cameraPos;
    vec4 prevCameraPos;
    vec4 windTime;
    // Muzzle-flash/explosion point lights -- see DynamicLight.h and
    // Application::drawFrame, which fills these each frame. xyz position,
    // w radius; rgb color, w intensity. A radius/intensity of 0 (the
    // default for any slot beyond however many lights are actually live)
    // means "inactive, skip" -- see the loop in main().
    vec4 dynamicLightPosRadius[MAX_DYNAMIC_LIGHTS];
    vec4 dynamicLightColorIntensity[MAX_DYNAMIC_LIGHTS];
    vec4 sunColor;
    vec4 skyZenith;
    vec4 skyHorizon;
    vec4 ambientColor;
    vec4 cloudColor;
    vec4 atmosphere;
    vec4 weaponEffects;
    vec4 scorchPositionRadius[16];
    vec4 scorchParameters[16];
    mat4 treeShadowMatrices[3];
    vec4 treeShadowWidths; // xyz: full world widths, w: depth range
    vec4 treeShadowParams; // x: 0 rays/1 PCF/2 PCSS, y: resolution, z: reset history, w: AO enabled
} frame;
