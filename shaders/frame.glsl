// Shared std140 layout; matches Pipeline::FrameUBO.
#define MAX_DYNAMIC_LIGHTS 4
#define MAX_WATER_WAVES 16
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
    // Last frame's tank world matrices, for the motion-vector buffer -- see
    // basic.vert and Pipeline::FrameUBO's comment.
    mat4 prevTankHullModel;
    mat4 prevTankTurretModel;
    mat4 prevTankBarrelModel;
    // Current view-proj without the TAA sub-pixel jitter -- motion vectors
    // must be jitter-free on both ends (prevViewProj is unjittered too), or
    // the jitter delta leaks into every velocity and TAA resamples history
    // off texel-center every frame. See Pipeline::FrameUBO.
    mat4 viewProjUnjittered;
    // Expanding wave sources on standing water (shell splashes, the tank's
    // wading wake). xy: centre XZ, z: current wavefront radius, w: wave-
    // slope amplitude, already decayed on the CPU (see WaterRipple::
    // waveSlope); 0 = inactive slot. Summed into the water shading normal
    // in basic.frag.
    vec4 waterWaves[MAX_WATER_WAVES];
} frame;
