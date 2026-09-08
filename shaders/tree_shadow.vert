#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "tree_wind.glsl"
layout(location = 0) in vec3 inPosition;
struct RasterInstance {
    mat4 model;
    vec4 wind;
    vec4 previousWind;
    vec4 foliageFade;
};
layout(std430, set = 0, binding = 1) readonly buffer InstanceTransforms {
    RasterInstance instances[];
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


void main() {
    RasterInstance instance = instanceData.instances[gl_InstanceIndex];
    vec4 world = instance.model * vec4(bendTreePosition(inPosition, instance.wind.xyz), 1.0);
    // unlit carries the cascade index in this depth-only pipeline.
    gl_Position = frame.treeShadowMatrices[int(pc.unlit)] * world;
}
