#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require
#extension GL_EXT_ray_tracing_position_fetch : require

// Enabled only by the tree foliage lighting pipeline. Its material contract
// lets the driver remove terrain, tank, water and effect paths at pipeline
// creation; the leaf lighting, ray budgets and history remain shared below.
layout(constant_id = 0) const bool kTreeFoliage = false;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragModelPos;
layout(location = 6) in vec3 fragPrevWorldPos;
layout(location = 7) in vec3 fragRayWorldPos;
layout(location = 8) in vec3 fragRayNormal;
layout(location = 9) flat in vec4 fragFoliageFade;

#include "frame.glsl"
layout(set = 0, binding = 2) uniform sampler2D environmentClouds;
layout(set = 0, binding = 3) uniform sampler2DArray treeShadowMap;
#include "tree_shadow_filter.glsl"

// Four material textures: a "high" (grass) pair and a "low" (gravel) pair,
// each pair patch-blended by a noise mask, with the high/low pair itself
// then blended by world-space height -- see the heightBlend push constant
// and Terrain's painting logic in main(). Only actually sampled/blended
// when pc.heightBlend is nonzero (terrain); every other draw binds the same
// plain texture into all four and these are simply never read.
layout(set = 1, binding = 0) uniform sampler2D materialTexHighA;
layout(set = 1, binding = 1) uniform sampler2D materialTexHighB;
layout(set = 1, binding = 2) uniform sampler2D materialTexLowA;
layout(set = 1, binding = 3) uniform sampler2D materialTexLowB;
// Terrain-only lookup generated once at startup. RG stores the two domain-
// warp noise values; BA stores the grass/gravel patch masks. Other material
// sets bind their regular texture here because their shader paths never read
// this binding.
layout(set = 1, binding = 4) uniform sampler2D terrainControlTex;
// Generated ground classification for the upgraded terrain: R exposed rock,
// G persistent moisture, B recent sediment/deposition. A is the enable flag:
// 0 keeps legacy analytic height rules; 1 enables generated material fields.
layout(set = 1, binding = 5) uniform sampler2D terrainFieldTex;
layout(set = 2, binding = 0) uniform accelerationStructureEXT sceneTLAS;
layout(set = 3, binding = 0) uniform sampler2D historyShadow;
// Independently-smoothed foliage-transmission factor -- see traceSoftShadow
// and the temporal-accumulation block in main() for why this can't share
// historyShadow's adaptive blend.
layout(set = 3, binding = 1) uniform sampler2D historyFoliage;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float unlit;
    float specularStrength;
    float heightBlend;
    float opacity;
    float reflectivity;
    // Perturbs the shading normal (specular/Fresnel/reflection only, not
    // the real diffuse/shadow-ray normal) with an animated ripple pattern
    // -- water only; 0 elsewhere leaves the normal untouched.
    float waveStrength;
    // Perturbs the *diffuse* normal using the material texture's own
    // luminance as a fake heightfield, via fragTangent -- track marks only;
    // 0 elsewhere. See main()'s litNormal.
    float bumpStrength;
    // Nonzero for a moving rigid body (currently just the tank) -- see
    // main()'s isTank and Pipeline::PushConstants::isDynamicObject's
    // comment for why this can't just be inferred from specularStrength.
    float isDynamicObject;
    float materialType;
    float isInstanced;
    vec4 tankSurface;
} pc;

layout(location = 0) out vec4 outColor;
// x: temporally-blended shadow factor. y: temporally-blended AO factor.
// z: view-distance at write time, used next frame to detect disocclusion
// (see main()). w: 2-frame-smoothed shadow disagreement, used next frame to
// tell a sustained real change from a one-frame noise spike (see main()).
layout(location = 1) out vec4 outShadowHistory;
// Fixed-alpha-smoothed foliage-transmission factor -- see the comment on
// historyFoliage above.
layout(location = 2) out float outFoliageHistory;
// UV-space (current minus previous) motion vector -- resolved and consumed
// by TonemapPass's debug visualization today; a future TAA blend pass will
// read it for real. See the velocity computation below main()'s existing
// prevClip reprojection block, which this reuses.
layout(location = 3) out vec2 outVelocity;

// Hard visibility test via a single ray query: 1.0 if nothing occludes the
// path from `origin` toward `direction`, 0.0 if something does.
// TerminateOnFirstHit since this is a boolean visibility test, not a
// closest-hit lookup -- any hit at all means occluded.
float traceShadow(vec3 origin, vec3 direction, float tMax, uint mask) {
    rayQueryEXT rayQuery;
    rayQueryInitializeEXT(rayQuery, sceneTLAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
                           mask, origin, 0.001, direction, tMax);
    while (rayQueryProceedEXT(rayQuery)) {}
    return rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT
               ? 1.0
               : 0.0;
}

// Plain two-tone sky gradient, no clouds -- factored out of skyColor below
// (further down, alongside the cloud-texture sampling it needs) so distance
// fog and traceReflection/the ambient helper below, which don't need clouds,
// can use just the smooth gradient. Using the full cloud-textured skyColor
// for fog made the cloud pattern visibly bleed onto nearby opaque geometry
// (tree trunks, rocks) any time that fragment's camera-to-surface direction
// pointed even slightly upward, since fog blends in this color starting
// close to the camera.
vec3 skyGradient(vec3 dir) {
    // Horizon haze stays pale, including for slightly downward fog rays.
    // Exponent 0.42 (was 0.55) pulls the zenith blue further down toward
    // the horizon: gameplay cameras mostly see sky at 10-40 degrees
    // elevation, and with the slower curve that band stayed a pale wash
    // instead of the clearly blue sky of the sunny-day reference.
    return mix(frame.skyHorizon.rgb, frame.skyZenith.rgb,
               pow(clamp(dir.y, 0.0, 1.0), 0.42));
}

// Directional ambient fill sourced from the same sky gradient the visible
// dome/fog use, replacing what used to be a flat frame.ambientColor tint
// applied identically regardless of surface orientation. A surface facing
// mostly up toward the sky (grass, a tank's turret roof) now skews toward
// the zenith tone; a surface facing sideways toward the horizon band skews
// toward the horizon tone -- the same "which patch of sky does this surface
// mostly see" idea skyGradient already uses for view rays, applied to
// surface normals instead. Luma-matched to the old flat ambientColor.rgb so
// the overall fill brightness this scene was tuned against doesn't shift --
// only its color and per-surface directional variation do.
//
// kSkyTintStrength keeps that variation partial rather than a full swap: a
// straight-up normal maps to raw skyZenith, which is a much more saturated
// blue than the pale constant it replaces (a real hemisphere of sky is
// mostly mid-elevation tones, not the exact zenith point, so going all the
// way there overstates it). Even after re-matching luma, that lost too much
// red relative to blue and, after the ACES tonemap, read as a visibly
// cooler/duller image rather than an equally-bright, differently-tinted one
// -- confirmed by comparing mean scene luminance before/after (changed by
// under 0.3/255, i.e. not a real brightness drop) against the same
// screenshots' visual "darker" impression, which tracked the red/blue
// channel shift instead. Blending back toward the original tone keeps the
// sky-tied directional effect visible while keeping most of the original
// warmth.
const float kSkyTintStrength = 0.4;
vec3 skyAmbientTint(vec3 n) {
    vec3 skyMix = mix(frame.skyHorizon.rgb, frame.skyZenith.rgb, n.y * 0.5 + 0.5);
    vec3 luma = vec3(0.299, 0.587, 0.114);
    float skyLuma = dot(skyMix, luma);
    float baseLuma = dot(frame.ambientColor.rgb, luma);
    vec3 tinted = skyMix * (baseLuma / max(skyLuma, 0.001));
    return mix(frame.ambientColor.rgb, tinted, kSkyTintStrength);
}

// Traces a closest-hit reflection ray (no TerminateOnFirstHit -- reflections
// need the *nearest* surface along the ray, not just any occluder). On a
// hit, uses GL_EXT_ray_tracing_position_fetch to read the hit triangle's
// actual vertex positions straight out of the acceleration structure
// (transformed to world space via the hit instance's object-to-world
// matrix), computes its true flat normal, and shades it with a simple
// unshadowed diffuse term -- giving the reflection real geometric occlusion
// awareness (nearby trees/rocks/terrain show up as darker patches) instead
// of a flat gradient, without needing a second, much larger system (per-
// BLAS vertex-color fetch) just to know the hit surface's exact albedo.
// Returns false on a miss, leaving the caller's existing fake sky/ground
// gradient as the fallback.
bool traceReflection(vec3 origin, vec3 direction, float tMax, out vec3 hitColor) {
    rayQueryEXT rayQuery;
    // Solid scene objects plus the selected tree reflection representation.
    rayQueryInitializeEXT(rayQuery, sceneTLAS, gl_RayFlagsOpaqueEXT, 0x09, origin, 0.001, direction,
                           tMax);
    while (rayQueryProceedEXT(rayQuery)) {}
    if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
        return false;
    }

    vec3 positions[3];
    rayQueryGetIntersectionTriangleVertexPositionsEXT(rayQuery, true, positions);
    mat4x3 objectToWorld = rayQueryGetIntersectionObjectToWorldEXT(rayQuery, true);
    vec3 p0 = objectToWorld * vec4(positions[0], 1.0);
    vec3 p1 = objectToWorld * vec4(positions[1], 1.0);
    vec3 p2 = objectToWorld * vec4(positions[2], 1.0);
    vec3 hitNormal = normalize(cross(p1 - p0, p2 - p0));

    vec3 toLight = normalize(-frame.lightDir.xyz);
    float diffuse = max(dot(hitNormal, toLight), 0.0);
    // No per-surface albedo lookup for the hit point -- a neutral tone
    // modulated by whether the hit face points toward or away from the
    // light reads as "reflecting nearby lit/shadowed geometry" honestly,
    // without guessing a color that might be wrong.
    hitColor = vec3(0.5) * (skyAmbientTint(hitNormal) * frame.ambientColor.w +
                            frame.sunColor.rgb * frame.sunColor.w * diffuse);
    return true;
}

// Cheap, texture-free per-pixel pseudo-random value for jittering shadow/AO
// rays without a fixed sampling pattern (which would band/tile visibly).
float interleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Continuous value noise for terrain/material variation.
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float valueNoise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Slow shared cloud drift, in texture UV per second of wind time. Added
// identically to the visible sky's lookup (skyColor) and the ground shadow
// projection (cloudShadow), so passing shade always tracks the drawn cloud
// field exactly. Roughly aligned with the smoke/foliage wind direction
// (+x dominant); ~1 world unit of shadow travel per second -- ambient
// motion, not weather-front speed.
const vec2 kCloudDrift = vec2(0.0045, 0.0011);

// One environment evaluation for both visible sky and reflection misses.
// Colour is linear here; the final surface/sky output is tonemapped once.
vec3 skyColor(vec3 dir) {
    vec3 color = skyGradient(dir);
    // Identical directional mapping and mip choice for raster sky and
    // reflection misses. Fade clouds into horizon haze before the projection
    // becomes singular. No per-fragment FBM or additional ray queries.
    vec2 uv = dir.xz / max(dir.y, 0.08) * frame.atmosphere.z - frame.windTime.x * kCloudDrift;
    float density = textureLod(environmentClouds, uv, 1.0).a;
    float cloudAlpha = smoothstep(frame.cloudColor.w, frame.cloudColor.w + 0.19, density)
                       * smoothstep(0.08, 0.18, dir.y);
    vec3 toSun = normalize(-frame.lightDir.xyz);
    float sunAlignment = max(dot(dir, toSun), 0.0);
    color += frame.sunColor.rgb * frame.sunColor.w * pow(sunAlignment, 48.0) * 0.10;
    float disk = smoothstep(cos(frame.atmosphere.w * 1.2), cos(frame.atmosphere.w), sunAlignment);
    color += frame.sunColor.rgb * frame.sunColor.w * disk * 4.0;
    vec3 cloudLight = mix(frame.skyHorizon.rgb * 0.70, frame.cloudColor.rgb,
                          1.0 - smoothstep(0.58, 0.86, density));
    cloudLight *= mix(vec3(1.0), frame.sunColor.rgb, sunAlignment * 0.4);
    return mix(color, cloudLight, cloudAlpha);
}

// Cloud shadows: project the shaded point along the sun direction onto a
// virtual cloud layer and sample the same cloud texture/coverage threshold
// the sky dome draws with. One textureLod of the already-bound cloud
// texture per fragment -- no rays, no extra bindings.
//
// WORLD-anchored, deliberately unlike skyColor's camera-anchored direction
// mapping: an earlier version subtracted frame.cameraPos.xz here to match
// the dome's projection exactly, and since the chase camera follows the
// tank, driving dragged the entire shade pattern across the ground with
// the vehicle -- shade read as attached to the tank, not the sky. Ground
// shadows must stay fixed in the world and move only with the wind drift;
// the resulting slow sky-to-shadow misregistration as the camera travels
// is imperceptible (the dome's clouds are effectively at infinity and no
// specific cloud/shadow pair can be visually matched anyway).
// Fixed world height of the virtual shadow-casting layer. Deliberately
// lower than the dome reads visually: one cloud mass spans ~0.2 UV of the
// texture, so the plane height sets the shadow feature size on the ground
// (~0.2*H/0.25 world units). At 90 a single shade patch covered ~70 units
// -- reading as a vignette on the whole map rather than passing cloud
// shade; 55 gives a few distinct ~45-unit patches across the play area.
const float kCloudPlaneHeight = 55.0;
const float kCloudShadowStrength = 0.6;
float cloudShadow(vec3 worldPos) {
    vec3 toSun = normalize(-frame.lightDir.xyz);
    // Fixed world height for the plane too (not camera-relative): terrain
    // relief is a few units against a 55-unit layer, so this stays a simple
    // constant without the camera's height dragging the projection around.
    float t = (kCloudPlaneHeight - worldPos.y) / max(toSun.y, 0.15);
    vec2 planePoint = worldPos.xz + toSun.xz * t;
    vec2 uv = planePoint / kCloudPlaneHeight * frame.atmosphere.z -
              frame.windTime.x * kCloudDrift;
    // Same mip as skyColor's visible clouds -- a coarser mip averaged the
    // density field toward its mean BEFORE the coverage threshold, which
    // erased exactly the peaks the threshold is looking for and left almost
    // no shadow at all. Softness comes from the wider smoothstep instead:
    // ground shade ramps in across the cloud's fringe rather than tracing
    // its drawn edge hard.
    float density = textureLod(environmentClouds, uv, 1.0).a;
    float cover = smoothstep(frame.cloudColor.w - 0.08, frame.cloudColor.w + 0.22, density);
    return 1.0 - cover * kCloudShadowStrength;
}

// Reimplements TrackTextureGenerator's tread-link ridge pattern as a
// procedural [0,1] height field (not sampled from the actual texture) --
// Needed because the real texture's own
// brown-on-brown color contrast is too low (~0.07 out of 1.0 in luminance)
// to give a usable bump signal, and any signal derived from a filtered/
// mipped/anisotropically-sampled texture read would vary with viewing
// distance anyway. uv.y here is the plain [0,1] mesh UV (see Mesh::quad);
// TrackTextureGenerator's own v spans [-1,1] instead, but fract() makes the
// period/phase match regardless -- see the derivation in the call site.
float trackHeightField(vec2 uv) {
    return smoothstep(0.42, 0.58, fract(uv.y * 12.0 + 0.5));
}

// Fine per-pixel specular variation for the tank -- reads as scratches/
// micro-imperfections in the paint or brushed-metal grain, giving the
// specular highlight real texture instead of one flat value per part. Uses
// fragUV directly: for the tank this is Tank::load's own synthetic
// per-axis triplanar projection (stable in the model's local space, not a
// real UV unwrap -- see its comment), so the pattern rides along with the
// hull through rotation/movement. Two well-separated octaves of plain
// value noise (deliberately not a regular grid -- an earlier version of
// this used a seam grid instead and it read as an obviously artificial
// checkerboard at normal viewing distance) so it doesn't look like one
// obviously repeating cell size either.
float tankSpecularGrain(vec2 uv) {
    return valueNoise2D(uv * 35.0 + vec2(7.3, 91.1)) * 0.6 +
           valueNoise2D(uv * 90.0 + vec2(41.2, 3.9)) * 0.4;
}

// Orthonormal basis around `n`, used to jitter a ray direction within a
// small cone (soft shadows) or hemisphere (AO) instead of a single fixed
// direction.
void buildBasis(vec3 n, out vec3 t, out vec3 b) {
    vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

const int kShadowSamples = 3;
// Extra rays spent only on pixels the first kShadowSamples already show to
// be in a penumbra (partially occluded) -- that's where few-sample noise is
// actually visible as dithered/jittering edges; a fully-lit or
// fully-shadowed pixel already reads as a clean flat value from
// kShadowSamples alone; and it's a small fraction of the screen (a thin band
// along each shadow boundary), so spending more rays there barely affects
// overall cost while cleaning up exactly the noisy region.
const int kShadowEdgeExtraSamples = 5;
const float kConeAngle = 0.05;
const float kFoliageConeAngle = 0.16;
const float kFoliageTransmission = 0.05;

// Match AccelerationStructure::Instance masks. Solid occluders keep their
// narrow sun cone. Foliage has a separate, wider cone and partial
// transmission.
//
// Returns the two factors SEPARATELY (x: solid visibility, y: foliage
// transmission) rather than pre-multiplied, because they get temporally
// smoothed independently in main() -- see that block's comment for why a
// combined value can't be filtered with one policy.
vec2 traceSoftShadow(vec3 origin, vec3 lightDir, float tMax, float noiseSeed,
                     int sampleCount, int edgeExtraSamples, int leafSampleCount) {
    vec3 t, b;
    buildBasis(lightDir, t, b);

    float sum = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        float u1 = fract(noiseSeed + float(i) * 0.6180339887);
        float u2 = fract(noiseSeed * 1.618 + float(i) * 0.3819660113);
        float angle = u1 * 6.2831853;
        float radius = kConeAngle * sqrt(u2);
        vec3 direction = normalize(lightDir + radius * (cos(angle) * t + sin(angle) * b));
        sum += traceShadow(origin, direction, tMax, frame.treeShadowParams.x > .5 ? 0x01u : 0x05u);
    }
    float solidVisibility = sum / float(sampleCount);
    if (solidVisibility > 0.001 && solidVisibility < 0.999) {
        int totalSamples = sampleCount + edgeExtraSamples;
        for (int i = sampleCount; i < totalSamples; ++i) {
            float u1 = fract(noiseSeed + float(i) * 0.6180339887);
            float u2 = fract(noiseSeed * 1.618 + float(i) * 0.3819660113);
            float angle = u1 * 6.2831853;
            float radius = kConeAngle * sqrt(u2);
            vec3 direction = normalize(lightDir + radius * (cos(angle) * t + sin(angle) * b));
            sum += traceShadow(origin, direction, tMax, frame.treeShadowParams.x > .5 ? 0x01u : 0x05u);
        }
        solidVisibility = sum / float(totalSamples);
    }
    if (frame.treeShadowParams.x > .5) return vec2(solidVisibility, 1.0);

    // Foliage transmission is irrelevant when a solid occluder already
    // blocks everything -- skip its rays. Its history simply holds at
    // whatever it last read while this stays true; harmless since the
    // product with solidVisibility reads as full shadow regardless, and it
    // resumes updating the moment solid visibility recovers.
    if (solidVisibility < 0.001) return vec2(0.0, 1.0);

    // Two canopy samples keep the broad penumbra from becoming stippled at
    // range, where temporal accumulation fills in the remaining coverage;
    // up close a wrong canopy estimate is both larger on screen and slower
    // to converge (more pixels, so more frames before every one of them has
    // resampled), so a couple of extra rays there are worth the cost. Don't
    // duplicate these for every solid edge ray.
    float leafVisibility = 0.0;
    for (int i = 0; i < leafSampleCount; ++i) {
        float leafAngle = fract(noiseSeed + 0.37 + float(i) * 0.5) * 6.2831853;
        float leafRadius = kFoliageConeAngle * sqrt(fract(noiseSeed * 1.618 + 0.71 + float(i) * 0.5));
        vec3 leafDir = normalize(lightDir + leafRadius * (cos(leafAngle) * t + sin(leafAngle) * b));
        leafVisibility += traceShadow(origin, leafDir, tMax, 0x02u);
    }
    // No cap on the miss fraction here: traceSoftShadow runs for every
    // shaded point in the scene, including ground with no tree anywhere
    // near it, where every leaf-masked sample is a guaranteed miss. Capping
    // that "fully open" case below 1.0 (an earlier version of this code did,
    // to compensate for the sparse/conservative leaf proxy undersampling
    // real occlusion near a tree) silently darkened the entire scene by a
    // flat amount, not just the ground under canopies -- the opposite of
    // making canopy shadow more visible, since it shrinks the contrast
    // between shadowed and unshadowed ground instead of widening it.
    //
    // The sparse, conservative leaf proxy (real gaps between sprays, each
    // inset well inside its spray's actual occupancy) means a point under
    // genuine canopy usually sees only a fraction of its few leaf-masked
    // rays register a hit, not all of them -- avgVisibility rarely reaches
    // 0 even directly under a dense crown. Raising it to a power leaves the
    // two anchor cases alone (0 stays 0, 1 stays 1) while pulling every
    // partial hit rate -- the common case under real foliage -- down much
    // closer to the fully-occluded end, so canopy shade reads as clearly
    // darker instead of a faint tint on top of full sun.
    float avgVisibility = leafVisibility / float(leafSampleCount);
    float shaped = avgVisibility * avgVisibility * (3.0 - 2.0 * avgVisibility);
    return vec2(solidVisibility, mix(kFoliageTransmission, 1.0, shaped));
}

const int kAOSamples = 4;
// Short rays: contact shadows, not scene-wide occlusion. Kept tight
// deliberately -- a wider radius means the tank's hull stays a genuine,
// correct occluder for any ground point within range long after it visually
// looks like the tank has driven past that point, which reads exactly like
// a temporal-filter ghost/trail even though it isn't one (tuning the blend
// alpha further made no difference for exactly this reason).
const float kAORadius = 0.35;
const float kAOStrength = 0.55; // how much a fully-occluded point can darken ambient -- pushed
                                 // back up (0.35 read as barely-there); the extra samples above
                                 // keep the per-frame noise-only swing small enough for the
                                 // dead-zone blend below to still tell it apart from a real change
// Cosine-weighted hemisphere sample around `n` -- standard importance
// sampling for a diffuse (Lambertian) AO/GI estimate, so more samples land
// near the normal (where they matter most) than near the horizon.
vec3 cosineSampleHemisphere(vec3 n, float u1, float u2) {
    float r = sqrt(u1);
    float theta = 6.2831853 * u2;
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - u1));
    vec3 t, b;
    buildBasis(n, t, b);
    return normalize(x * t + y * b + z * n);
}

float traceAO(vec3 origin, vec3 normal, float seedBase, int sampleCount, float radius, float strength) {
    if (sampleCount <= 0) return 1.0;
    float occlusion = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        // Different irrational offsets than the shadow jitter's, so the two
        // don't end up correlated (same seedBase, different sample set).
        float u1 = fract(seedBase + float(i) * 0.7548776662);
        float u2 = fract(seedBase * 1.3247179572 + float(i) * 0.5698402910);
        vec3 sampleDir = cosineSampleHemisphere(normal, u1, u2);
        occlusion += 1.0 - traceShadow(origin, sampleDir, radius, 0x07u);
    }
    return 1.0 - strength * (occlusion / float(sampleCount));
}

// This shader writes raw linear HDR color (no tonemap, no exposure) into a
// ResolveTarget; shaders/tonemap.frag is now the single place that maps the
// resolved HDR scene down to the swapchain's sRGB output. See that file's
// comment for why (highlight clipping on an 8-bit target) and the ACES
// curve itself, both moved there from here.

const float kPi = 3.14159265359;

// Trowbridge-Reitz/GGX normal distribution -- how tightly microfacet normals
// cluster around the half vector. alpha is roughness*roughness (the usual
// perceptual-roughness-to-alpha remap, keeps the low end of the roughness
// slider from feeling like it does nothing).
float distributionGGX(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * d * d, 1e-6);
}

// Smith joint masking-shadowing term, direct-lighting alpha/2 remap (Karis
// 2013) -- accounts for microfacets occluding/shadowing each other.
float geometrySmithGGX(float NdotV, float NdotL, float alpha) {
    float k = alpha * 0.5;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

// Schlick's Fresnel approximation. f0 is normal-incidence reflectance:
// ~0.02-0.06 for dielectrics (paint, stone, soil), the material's own tint
// for metals.
vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    float m = clamp(1.0 - cosTheta, 0.0, 1.0);
    return f0 + (vec3(1.0) - f0) * (m * m * m * m * m);
}

// diffuseWeight is the energy left over for the diffuse term after Fresnel
// reflectance and metalness are accounted for -- replaces the old flat
// mix(1.0,0.75,specularStrength) diffuse-darkening hack with the BRDF's own
// real split (only actually reduces diffuse at grazing angles or for
// metals, not as a constant per-material tax). specular is the full
// Cook-Torrance D*G*F/(4*NdotV*NdotL) term already multiplied by NdotL, so
// callers just add it in scaled by light color/shadow/intensity.
struct BrdfResult { vec3 diffuseWeight; vec3 specular; };

// f0 is the caller's already-resolved normal-incidence reflectance --
// mix(vec3(dielectricConst), metalTintColor, metalness), NOT derived in
// here, since a metal's Fresnel tint is its own steel/tarnish colour, not
// its (possibly wear-darkened) diffuse albedo texture and not plain white.
BrdfResult evaluateGGX(vec3 N, vec3 V, vec3 L, float roughness, float metalness, vec3 f0) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float alpha = max(roughness * roughness, 0.0025);
    vec3 F = fresnelSchlick(VdotH, f0);
    float D = distributionGGX(NdotH, alpha);
    float G = geometrySmithGGX(NdotV, NdotL, alpha);
    BrdfResult r;
    r.specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4) * NdotL;
    r.diffuseWeight = (vec3(1.0) - F) * (1.0 - metalness);
    return r;
}

// UV-space (current minus previous) motion vector for this fragment, for
// the TAA resolve (see TaaBlendPass/taa_blend.frag). Built from the
// UNJITTERED matrices on both ends -- frame.proj carries the TAA sub-pixel
// jitter for rasterization, but the history image TAA reprojects into is a
// converged, effectively unjittered image, so a velocity containing the
// frame-to-frame jitter delta would resample history off texel-center every
// frame (permanent bilinear blur + visible sub-pixel wobble). Shares the
// shadow-history reprojection's Y-flip convention (see that block's
// comment in main) so both consumers of these images agree on orientation.
vec2 computeScreenVelocity() {
    vec4 currentClip = frame.viewProjUnjittered * vec4(fragWorldPos, 1.0);
    vec4 prevClip = frame.prevViewProj * vec4(fragPrevWorldPos, 1.0);
    if (currentClip.w <= 0.001 || prevClip.w <= 0.001) return vec2(0.0);
    vec2 currentNDC = currentClip.xy / currentClip.w;
    vec2 prevNDC = prevClip.xy / prevClip.w;
    vec2 currentUV = vec2(currentNDC.x * 0.5 + 0.5, 0.5 - currentNDC.y * 0.5);
    vec2 prevUV = vec2(prevNDC.x * 0.5 + 0.5, 0.5 - prevNDC.y * 0.5);
    return currentUV - prevUV;
}

// Exponential distance fog, tinted by the plain sky gradient (not
// skyColor's cloud-textured version -- see skyGradient's comment) -- gives
// the terrain's ~255-unit corner-to-corner diagonal (see Camera::
// projection's far plane) a depth cue and hazes the far edge toward the
// horizon instead of it staying full-contrast right up to the view's far
// clip. frame.atmosphere.x keeps the near/gameplay range (chase cam sits
// ~8 units back, see Camera::followTarget) completely clear so fog only
// ever shows up well beyond the action; density is applied to distance
// past that start, not total distance, so it ramps in gradually rather
// than jumping straight to its far-clip value at the start line.
void main() {
    // Matches Application's leafPc defaults; opacity remains per draw.
    float materialType = kTreeFoliage ? 2.0 : pc.materialType;
    float heightBlend = kTreeFoliage ? 0.0 : pc.heightBlend;
    float unlit = kTreeFoliage ? 0.0 : pc.unlit;
    float isDynamicObject = kTreeFoliage ? 0.0 : pc.isDynamicObject;
    float bumpStrength = kTreeFoliage ? 0.0 : pc.bumpStrength;
    float waveStrength = kTreeFoliage ? 0.0 : pc.waveStrength;
    float reflectivity = kTreeFoliage ? 0.0 : pc.reflectivity;

    // Soft procedural weapon cards; all edges reach zero inside the quad.
    // The effects pipeline disables depth and history writes, but tests depth.
    if (materialType > 7.5 && materialType < 9.5) {
        vec2 p = fragUV * 2.0 - 1.0;
        float alpha;
        vec3 color;
        if (materialType < 8.5) {
            float t = fragUV.y;
            float width = mix(.48, .025, t) + .13*sin(t*3.14159);
            float lobe = 1.0-smoothstep(width*.3,width,abs(p.x));
            alpha = lobe * smoothstep(0.0,.06,t) * (1.0-smoothstep(.5,1.0,t));
            float core = (1.0-smoothstep(0.0,width*.65,abs(p.x))) * (1.0-t);
            color = mix(vec3(1.0,.22,.035),vec3(3.8,2.8,1.3),core);
            if (pc.tankSurface.z > .5) {
                alpha = 1.0-smoothstep(.1,.95,length(p));
                color = vec3(2.8,1.9,.75);
            }
        } else {
            float age = pc.tankSurface.x;
            float seed = pc.tankSurface.y;
            if (pc.tankSurface.w > 1.5) {
                // Water spray (Smoke::spray, shell splashes): thrown
                // droplets, not combustion smoke -- brighter and cooler
                // than any smoke, with a far more ragged edge (droplet
                // clumps breaking off the sheet) and no fire interior.
                // The noise scrolls DOWN the card as it ages so the
                // fringe reads as water falling back, even while the
                // card itself still drifts up.
                float spr = valueNoise2D(p*4.6 + vec2(seed, -age*2.4));
                float radius = length(p) + (spr-.5)*.42;
                alpha = (1.0-smoothstep(.12,.8,radius)) * mix(.45,1.0,spr);
                color = mix(vec3(.5,.56,.6), vec3(.86,.91,.96), spr);
            } else {
                float noise = valueNoise2D(p*3.2 + vec2(seed, age*.7));
                float radius = length(p) + (noise-.5)*.22;
                alpha = (1.0-smoothstep(.25,.95,radius)) * mix(.6,1.0,noise);
                color = mix(vec3(.25,.26,.25),vec3(.48,.49,.47),age);
                if (pc.tankSurface.w > .5) {
                    // Explosion soot (Smoke::soot): a much darker column
                    // than muzzle smoke, with an HDR fire glow lighting its
                    // interior from below while the burst is young -- the
                    // glow follows the same noise that shapes the puff so it
                    // reads as flame showing through gaps, then dies out and
                    // leaves cooling black smoke that pales slightly as it
                    // disperses.
                    color = mix(vec3(.085,.08,.075), vec3(.30,.295,.29), age);
                    float glow = (1.0-smoothstep(.02,.35,age)) * smoothstep(.35,.9,noise);
                    color += vec3(2.6,.9,.18) * glow;
                }
            }
        }
        alpha *= pc.opacity;
        // No discard needed: this pipeline writes neither depth nor history.
        // Zero alpha is also compatible with devices without shader demotion.
        if (alpha < .002) alpha = 0.0;
        outColor = vec4(color, alpha);
        outShadowHistory = vec4(0);
        outFoliageHistory = 1.0;
        // Never lands: the effects pipeline write-masks the velocity
        // attachment (see Pipeline.cpp) so these translucent cards keep the
        // opaque background's velocity underneath -- the standard TAA
        // treatment of transparents. Writing anything here would stomp the
        // background's velocity across the whole billboard quad, including
        // its fully transparent texels (this pipeline draws without
        // discard -- see the alpha comment above).
        outVelocity = vec2(0.0);
        return;
    }
    if (materialType > 3.5 && materialType < 4.5) {
        vec3 direction = normalize(fragWorldPos - frame.cameraPos.xyz);
        outColor = vec4(skyColor(direction), 1.0);
        outShadowHistory = vec4(1.0, 1.0, length(fragWorldPos - frame.cameraPos.xyz), 0.0);
        outFoliageHistory = 1.0;
        // Real reprojection velocity, not zero: under camera rotation the
        // sky visibly moves across the screen, and a zero velocity would
        // make TAA blend the un-moved previous sky into it (smearing).
        outVelocity = computeScreenVelocity();
        return;
    }
    float currentViewDist = length(frame.cameraPos.xyz - fragWorldPos);
    // Domain-warp the terrain's sample UV with a low-frequency (world-space)
    // noise offset so its many texture repeats (60 across the current
    // 180-unit terrain -- see kTextureRepeatsPerUnit in Terrain.cpp) look
    // subtly different from each other instead of tiling as an exact,
    // eye-catching grid. The warp's own frequency is well below the
    // texture's tiling period, so it doesn't introduce a new repeating
    // pattern of its own. Gated to terrain (heightBlend) only -- anything
    // else's UVs are meaningful exact mappings (e.g. the crate's one UV
    // island per face) that warping would visibly distort.
    vec2 sampleUV = fragUV;
    // Keep the terrain texture projection continuous in world space.
    if (materialType > 0.5 && materialType < 1.5) {
        sampleUV = fragWorldPos.xz / 3.0;  // matches Terrain.cpp
    }
    vec4 terrainControl = vec4(0.0);
    vec4 terrainFields = vec4(0.0);
    float fieldsOn = 0.0;
    if (heightBlend > 0.5) {
        // The control map covers the whole 180-unit terrain. Half-texel
        // inset maps its world-space edges to texel centres, preserving the
        // CPU-baked noise without sampling beyond the clamped texture edge.
        const float kTerrainWorldSize = 180.0;
        const float kControlTexel = 1.0 / 512.0;
        vec2 controlUV = (fragWorldPos.xz / kTerrainWorldSize + 0.5) * (1.0 - kControlTexel) +
                         0.5 * kControlTexel;
        terrainControl = texture(terrainControlTex, controlUV);
        terrainFields = texture(terrainFieldTex, controlUV);
        fieldsOn = step(0.25, terrainFields.a);
        sampleUV += (terrainControl.rg - 0.5) * 0.6;
    }

    vec4 texSample = texture(materialTexHighA, sampleUV);
    vec3 texColor = texSample.rgb;
    // Cheap per-pixel surface detail for terrain's diffuse lighting, derived
    // straight from the albedo texture's own luminance gradient rather than
    // a separate normal-map texture/descriptor slot -- terrain's UV already
    // aligns 1:1 (uniform scale, no rotation) with world X/Z (see
    // Terrain::buildMesh), so, like water's wave ripple below, the resulting
    // perturbation can be added directly in world space without a tangent-
    // space transform. Always sampled from materialTexHighA (the base grass
    // texture) regardless of which material actually blends in at this
    // pixel -- this is meant to read as fine micro-detail under lighting,
    // not to exactly track the grass/gravel blend.
    vec2 terrainBump = vec2(0.0);
    float terrainRockiness = 0.0;
    if (heightBlend > 0.5) {
        // Terrain: within each zone (grass, gravel), patch-blend between two
        // texture variants using a large-scale noise mask, so the ground
        // reads as naturally varied -- patches of lusher/drier grass,
        // lighter/darker gravel -- instead of one texture repeated
        // everywhere. Two octaves per zone for a less obviously-round patch
        // shape; independent noise (different frequency/offset) per zone so
        // grass patches and gravel patches don't line up with each other or
        // with the height-based zone boundary below.
        float grassPatch = terrainControl.b;
        float gravelPatch = terrainControl.a;
        vec3 grassColor =
            mix(texColor, texture(materialTexHighB, sampleUV).rgb, smoothstep(0.4, 0.6, grassPatch));
        vec3 gravelColor = mix(texture(materialTexLowA, sampleUV).rgb, texture(materialTexLowB, sampleUV).rgb,
                                smoothstep(0.4, 0.6, gravelPatch));
        // Height gradients must compare raw texture samples. Sediment and mud
        // colour tints below must not introduce a false normal tilt.
        vec3 gravelHeightColor = gravelColor;
        // Fresh deposits read as warmer, sandier silt than the parent gravel.
        gravelColor = mix(gravelColor, gravelColor * vec3(1.08, 1.0, 0.82),
                          terrainFields.b * fieldsOn);
        // The innermost bank strip (moisture saturates within ~0.6 m of the
        // waterline) is trodden wet earth, not gravel: tint it dark mud.
        float bankMud = smoothstep(0.82, 0.97, terrainFields.g) * fieldsOn;
        gravelColor = mix(gravelColor, gravelColor * vec3(0.92, 0.74, 0.52), bankMud);

        // Fade to the low-point (gravel) blend in valleys. Center threshold
        // tuned against the heightmap's actual range (roughly -2.2..-4 on
        // the low end, seed-dependent, now that HeightmapGenerator layers a
        // plateau and a steepest-descent-traced river valley on top of the
        // base rolling hills, versus the plain +-2.2 of a hills-only
        // version -- see HeightmapGenerator.cpp). The
        // threshold itself is jittered by a low-frequency noise rather than
        // being a pure function of height -- a plain height threshold draws
        // an artificial contour around every hill. Reuse the two finer patch
        // masks to break up the edge locally. Size the transition from the
        // terrain slope so it covers a short, roughly consistent distance
        // across the ground. Within that transition, use fine world-space
        // noise as a coverage threshold: small grass and gravel patches
        // interlock instead of forming a solid intermediate-colour ring.
        float rockyThreshold =
            -1.7 + (valueNoise2D(fragWorldPos.xz * 0.05 + vec2(153.2, 88.7)) - 0.5) * 1.05;
        float boundaryBreakup = (grassPatch - gravelPatch) * 0.28;
        float rockyBoundary = rockyThreshold - (fragWorldPos.y + boundaryBreakup);
        vec3 terrainNormal = normalize(fragNormal);
        float terrainSlope = length(terrainNormal.xz) / max(abs(terrainNormal.y), 0.15);
        float physicalBlendWidth = clamp(terrainSlope * 0.9, 0.035, 0.14);
        float blendCoverage = smoothstep(-physicalBlendWidth, physicalBlendWidth, rockyBoundary);
        // Generated terrain drives exposure from its actual soil/erosion
        // history instead of the legacy height threshold: thin or scarred
        // soil reads as rock and strong deposition reads as bare bars. The
        // patch masks still break the boundary into interlocking pieces.
        // Deposition only reads as a bare bar right against the water: the
        // moisture gate keeps broad valley-floor sediment under its grass.
        float sedimentBar = max(terrainFields.b - 0.5, 0.0) * 2.0 *
                            smoothstep(0.55, 0.85, terrainFields.g);
        // Bare mud margins hug every stream and lake: grass gives way to wet
        // earth right at the waterline instead of running into the water.
        float fieldCoverage = clamp(terrainFields.r + sedimentBar + bankMud * 0.85 +
                                    boundaryBreakup * 0.5, 0.0, 1.0);
        blendCoverage = mix(blendCoverage, fieldCoverage, fieldsOn);
        float materialPattern =
            0.1 + valueNoise2D(fragWorldPos.xz * 0.9 + vec2(37.1, 214.6)) * 0.8;
        float patternEdgeWidth = max(fwidth(materialPattern) * 1.5, 0.025);
        float rockiness = smoothstep(materialPattern - patternEdgeWidth,
                                     materialPattern + patternEdgeWidth, blendCoverage);
        // Saturated lake beds and the inner bank are continuously bare mud.
        // Do this after the noise threshold so it cannot punch grass patches
        // through the submerged material, even on flat, soil-filled floors.
        rockiness = max(rockiness, bankMud);

        // Steep ground reads as rocky regardless of height -- the plateau's
        // raised edges and the river/valley's banks (see HeightmapGenerator)
        // are exactly the steepest parts of the terrain, and real slopes
        // that steep don't hold a grass root system/soil the way flatter
        // ground does; they show bare rock/scree instead. Uses the raw
        // interpolated fragNormal directly (not the later-computed `normal`
        // variable, which doesn't exist yet at this point in main()) --
        // steepness only needs the geometric slope, not the fully
        // normalized/bump-mapped shading normal. smoothstep range chosen so
        // gentle hillsides (most of the map) stay grass and only genuinely
        // steep faces pick this up.
        float steepness = 1.0 - terrainNormal.y;
        float slopeRockiness = smoothstep(0.32, 0.62, steepness);
        rockiness = max(rockiness, slopeRockiness);
        terrainRockiness = rockiness;
        texColor = mix(grassColor, gravelColor, rockiness);

        // Damp, low-lying ground: distinct from the low+steep gravel switch
        // above -- flat valley floors keep their grass/gravel texture but
        // darken and green slightly where they're both low and sheltered
        // (flat), the same height/noise-jittered-threshold idiom as the
        // terrain's existing rocky threshold, applied to its own materials.
        if (currentViewDist < 45.0) {
            float dampFlatness = 1.0 - smoothstep(0.12, 0.32, steepness);
            float dampThreshold =
                -1.7 + (valueNoise2D(fragWorldPos.xz * 0.07 + vec2(203.4, 61.8)) - 0.5) * 1.2;
            float dampHeightBias =
                1.0 - smoothstep(dampThreshold - 0.4, dampThreshold + 1.0, fragWorldPos.y);
            float dampPattern = valueNoise2D(fragWorldPos.xz * 1.6 + vec2(12.9, 88.4));
            // Generated moisture (bank strips, storm-wetted floors, gullies)
            // replaces the low-height guess wherever the fields are bound.
            float dampAmount = mix(dampHeightBias, terrainFields.g, fieldsOn);
            float damp = dampFlatness * dampAmount * smoothstep(0.3, 0.7, dampPattern) * 0.35
                       * (1.0 - smoothstep(30.0, 45.0, currentViewDist));
            texColor = mix(texColor, texColor * vec3(0.75, 0.88, 0.72), damp);
        }

        const float kTerrainBumpTexelStep = 1.0 / 512.0;  // matches kTerrainTextureRes in Application.cpp
        vec3 luminanceWeights = vec3(0.299, 0.587, 0.114);
        // Follow the material visible at this point. Previously every pixel,
        // including exposed gravel, used the first grass texture as height.
        // Micro-bump is not resolvable in the distance. Nearby, sample only
        // the dominant grass/rock pair away from the material boundary. In
        // the narrow transition, calculate each material's gradient against
        // its own centre colour before blending the gradients. Comparing a
        // pure-material offset sample with the already mixed centre colour
        // creates a large false height delta -- the visible ring that used
        // to follow the grass/gravel boundary.
        if (currentViewDist < 60.0) {
            const float kBumpBlendStart = 0.35;
            const float kBumpBlendEnd = 0.65;
            vec2 grassBump = vec2(0.0);
            vec2 gravelBump = vec2(0.0);

            if (rockiness < kBumpBlendEnd) {
                float patchBlend = smoothstep(0.4, 0.6, grassPatch);
                vec3 bumpU = mix(texture(materialTexHighA,
                                         sampleUV + vec2(kTerrainBumpTexelStep, 0.0)).rgb,
                                 texture(materialTexHighB,
                                         sampleUV + vec2(kTerrainBumpTexelStep, 0.0)).rgb,
                                 patchBlend);
                vec3 bumpV = mix(texture(materialTexHighA,
                                         sampleUV + vec2(0.0, kTerrainBumpTexelStep)).rgb,
                                 texture(materialTexHighB,
                                         sampleUV + vec2(0.0, kTerrainBumpTexelStep)).rgb,
                                 patchBlend);
                float heightCenter = dot(grassColor, luminanceWeights);
                grassBump = vec2(dot(bumpU, luminanceWeights) - heightCenter,
                                 dot(bumpV, luminanceWeights) - heightCenter) * 2.2;
            }
            if (rockiness > kBumpBlendStart) {
                float patchBlend = smoothstep(0.4, 0.6, gravelPatch);
                vec3 bumpU = mix(texture(materialTexLowA,
                                         sampleUV + vec2(kTerrainBumpTexelStep, 0.0)).rgb,
                                 texture(materialTexLowB,
                                         sampleUV + vec2(kTerrainBumpTexelStep, 0.0)).rgb,
                                 patchBlend);
                vec3 bumpV = mix(texture(materialTexLowA,
                                         sampleUV + vec2(0.0, kTerrainBumpTexelStep)).rgb,
                                 texture(materialTexLowB,
                                         sampleUV + vec2(0.0, kTerrainBumpTexelStep)).rgb,
                                 patchBlend);
                float heightCenter = dot(gravelHeightColor, luminanceWeights);
                gravelBump = vec2(dot(bumpU, luminanceWeights) - heightCenter,
                                  dot(bumpV, luminanceWeights) - heightCenter) * 5.0;
            }

            float bumpBlend = smoothstep(kBumpBlendStart, kBumpBlendEnd, rockiness);
            terrainBump = mix(grassBump, gravelBump, bumpBlend);
            // Saturated sediment softens the gravel's sharp micro-relief.
            terrainBump *= mix(1.0, 0.2, bankMud);
        }
    }
    bool tankMaterial = materialType > 4.5 && materialType < 7.5;
    bool rockMaterial = materialType > 2.5 && materialType < 3.5;
    bool barkMaterial = materialType > 9.5 && materialType < 10.5;
    bool isLeaf = materialType > 1.5 && materialType < 2.5;
    // Shared PBR base parameters -- perceptual roughness, metalness, and
    // dielectric normal-incidence reflectance (F0). No per-pixel roughness/
    // metal texture exists yet (every TextureGenerator is base-colour-only),
    // so these are per-materialType constants, the same idiom already used
    // for the per-type specularStrength overrides further below. Tank parts
    // overwrite these below once wear/dust/soot are known; water overwrites
    // them once isWater is known.
    float roughness = 0.55;      // generic/crates default
    float metalness = 0.0;
    float f0Dielectric = 0.04;
    // Only meaningful once metalness > 0 -- a metal's Fresnel tint is its
    // own steel/tarnish colour, not its diffuse albedo texture (which for
    // the tank is deliberately darkened for wear/dust/soot, and would make
    // any metal tinted by it look wrongly near-black) and not plain white.
    vec3 metalTint = vec3(1.0);
    if (materialType > 0.5 && materialType < 1.5) {        // terrain: soil/grass/gravel
        roughness = 0.85; f0Dielectric = 0.035;
        // Field-driven response: exposed rock/gravel carries a broader
        // mineral sheen than turf, and damp bank soil takes a wet-ground
        // gloss with a slightly stronger Fresnel term -- the sun sheen you
        // read on real mud at the waterline. Legacy terrain has zero fields
        // (terrainFields.a = 0) and keeps only the rockiness term.
        float wetGround = smoothstep(0.5, 1.0, terrainFields.g) * fieldsOn;
        roughness = mix(roughness, 0.7, terrainRockiness * 0.6);
        roughness = mix(roughness, 0.42, wetGround);
        f0Dielectric = mix(f0Dielectric, 0.05, wetGround);
    } else if (isLeaf) {                                   // foliage: waxy leaf sheen
        roughness = 0.55; f0Dielectric = 0.02;
    } else if (rockMaterial) {                             // stone: broad mineral sheen
        roughness = 0.75; f0Dielectric = 0.035;
    } else if (barkMaterial) {
        roughness = 0.8; f0Dielectric = 0.035;
    }
    // Only tank colour channels carry baked edge distances. Natural stone
    // retains its authored tint, independently of geometric feature masks.
    vec3 albedo = tankMaterial ? texColor * 0.95 : fragColor * texColor;
    if (((materialType > 1.5 && materialType < 2.5) || barkMaterial) && currentViewDist < 45.0) {
        // Bark/leaf/shrub meshes are all authored with their ground contact
        // point at local y=0 (see Mesh::shrub/buildTreeBranch's comments),
        // so the raw model-space height doubles as "how close to the
        // ground" -- same idiom as the tank's own tankDust term below
        // (fragModelPos.y), just without a per-instance bounds push
        // constant like the tank's tankSurface to normalize against.
        float groundDirt = (1.0 - smoothstep(0.05, 0.55, fragModelPos.y)) * 0.35
                         * (1.0 - smoothstep(30.0, 45.0, currentViewDist));
        albedo = mix(albedo, albedo * vec3(0.62, 0.58, 0.48), groundDirt);
    }
    float tankWear = 0.0;
    float tankDust = 0.0;
    float tankSoot = 0.0;
    float tankGrain = 0.5;
    if (tankMaterial) {
        bool tracks = materialType > 5.5 && materialType < 6.5;
        bool barrel = materialType > 6.5;
        // Filter the existing specular map once and share it between
        // highlight strength and roughness. Unresolved grain becomes the
        // average finish instead of sparkling at driving-camera distances.
        float grainFade = 1.0 - smoothstep(0.015, 0.06, length(fwidth(fragUV)));
        tankGrain = mix(0.5, tankSpecularGrain(fragUV), grainFade);
        // Object-space noise stays attached during hull, turret and gun motion.
        float patches = valueNoise2D(fragModelPos.xz * 5.0 + fragModelPos.y * vec2(1.7, 2.3));
        float edgeDistance = min(fragColor.x, min(fragColor.y, fragColor.z));
        float edgeWidth = mix(0.015, 0.030, patches);
        // Use the surface's pixel footprint, not derivatives of edge masks:
        // active-edge channels can change across adjacent triangles. Broader
        // low-frequency scuffs replace the undersampled chip-noise threshold.
        float pixelSize = max(length(dFdx(fragModelPos)), length(dFdy(fragModelPos)));
        float edgeAA = max(pixelSize, 0.001);
        float resolvedWear = smoothstep(0.8, 2.0, edgeWidth / edgeAA);
        float scuff = smoothstep(0.30, 0.70, patches);
        tankWear = (1.0 - smoothstep(edgeWidth - edgeAA * 0.5,
                                    edgeWidth + edgeAA * 0.5, edgeDistance)) *
                   scuff * resolvedWear * 0.45;
        float height = (fragModelPos.y - pc.tankSurface.x) * pc.tankSurface.y;
        tankDust = (1.0 - smoothstep(0.12, 0.62, height + (patches - 0.5) * 0.22)) *
                   mix(0.10, tracks ? 0.38 : 0.25, patches);
        if (barrel) {
            tankDust = 0.0;
            float muzzleDistance = (pc.tankSurface.z - fragModelPos.z) * pc.tankSurface.w;
            tankSoot = (1.0 - smoothstep(0.015, 0.16, muzzleDistance)) * mix(0.65, 0.90, patches);
        }
        if (tracks || barrel) albedo = mix(albedo, vec3(0.028, 0.031, 0.034), 0.35);
        albedo *= mix(0.96, 1.04, patches);
        albedo = mix(albedo, vec3(0.12, 0.13, 0.14), tankWear * (tracks ? 0.65 : 0.38));
        albedo = mix(albedo, vec3(0.15, 0.125, 0.085), tankDust);
        albedo = mix(albedo, vec3(0.006, 0.005, 0.004), tankSoot);
        // Base roughness/metalness per part -- painted armour is a
        // dielectric coat over steel, tracks mix worn metal pins with
        // rubber pads, the barrel is bare/oiled gun steel. Wear exposes more
        // bare metal underneath (smoother, more metallic); dust/soot cake
        // the surface in a dielectric layer (rougher, less metallic).
        // Grain varies the highlight width as well as its brightness.
        float tankRoughness = tracks ? 0.70 : (barrel ? 0.38 : 0.56);
        tankRoughness += (tankGrain - 0.5) * 0.18 + (patches - 0.5) * 0.10;
        metalness = tracks ? 0.45 : (barrel ? 0.85 : 0.05);
        f0Dielectric = 0.045; // dielectric floor once dust/soot cake the metal below
        // Retain the darker worn/oiled steel tint under the broad sky fill.
        metalTint = tracks ? vec3(0.42, 0.40, 0.38) : vec3(0.32, 0.33, 0.34); // worn/oiled steel
        tankRoughness = mix(tankRoughness, tracks ? 0.42 : 0.30, tankWear);
        metalness = mix(metalness, max(metalness, 0.6), tankWear);
        tankRoughness = mix(tankRoughness, 0.95, clamp(tankDust + tankSoot, 0.0, 1.0));
        metalness = mix(metalness, 0.0, clamp(tankDust + tankSoot, 0.0, 1.0));
        roughness = tankRoughness;
    }
    // Texture alpha times the per-draw opacity (PushConstants::opacity) --
    // both are 1.0 for every opaque draw in the scene, so this only actually
    // does something for fading ground decals like TrackMark, whose texture
    // has a soft alpha falloff and whose opacity decreases as it ages.
    float finalAlpha = texSample.a * pc.opacity;

    // Ground scorch is part of the terrain material, not a hovering plane.
    // This follows every terrain triangle and adds no geometry or RT instances.
    if (materialType > .5 && materialType < 1.5 && pc.isInstanced < .5) {
        float burn = 0.0;
        for (int i=0; i<int(frame.weaponEffects.x); ++i) {
            vec4 mark = frame.scorchPositionRadius[i];
            vec3 delta = fragWorldPos-mark.xyz;
            vec2 q = delta.xz/mark.w;
            if (dot(q,q)>1.3 || abs(delta.y)>mark.w*2.0) continue;
            float noise = valueNoise2D(q*5.0 + mark.xz);
            float edge = length(q) + (noise-.5)*.24;
            float mask = (1.0-smoothstep(.3,1.0,edge))*mix(.7,1.0,noise);
            burn = max(burn,mask*frame.scorchParameters[i].x);
        }
        albedo *= mix(vec3(1),vec3(.12,.095,.07),burn);
    }

    if (unlit > 0.5) {
        outColor = vec4(albedo, finalAlpha);
        outShadowHistory = vec4(1.0, 1.0, 50000.0, 0.0);
        outFoliageHistory = 1.0;
        // Real reprojection velocity: this path covers both genuinely
        // static geometry (the boundary wall) and transient quads (dust
        // puffs, impact flashes). Camera-motion reprojection is exactly
        // right for the former and a close approximation for the latter
        // (their own drift between frames is small; the TAA neighborhood
        // clamp bounds the residual error). A zero would smear everything
        // here when the camera moves; a sentinel would kill TAA over the
        // full quad footprint including its transparent texels.
        outVelocity = computeScreenVelocity();
        return;
    }

    vec3 normal = normalize(fragNormal);
    vec3 toLight = normalize(-frame.lightDir.xyz);

    // Offset the ray origin along the normal to avoid self-shadowing
    // ("shadow acne") from the surface the ray starts on. Directional light
    // has no real distance limit, so tMax just needs to comfortably exceed
    // the scene's extent (terrain worldSize is 180, corner-to-corner
    // diagonal ~255) -- this also doubles as traceReflection's tMax, and a
    // near-horizontal reflection ray can travel close to that full diagonal
    // before hitting anything or reaching open sky.
    const float kShadowBias = 0.02;
    const float kShadowTMax = 500.0;
    // Mix in a per-frame counter (golden-ratio additive recurrence) so each
    // frame jitters differently even for a completely static camera/scene --
    // without this, temporal accumulation has nothing to actually average
    // over time (see the comment on cameraPos.w in Application::drawFrame).
    float noiseSeed =
        fract(interleavedGradientNoise(gl_FragCoord.xy) + frame.cameraPos.w * 0.6180339887);
    vec3 rayNormal = normalize(fragRayNormal);
    vec3 rayOrigin = fragRayWorldPos + rayNormal * kShadowBias;
    // Spend rays where their detail is resolvable. Temporal accumulation
    // converges the reduced medium/far samples over successive frames, while
    // the near gameplay area keeps the original quality. AO's 0.35-unit
    // contact detail is too small to see at long range, so it can disappear
    // entirely beyond 45 units without changing the readable image.
    int shadowSamples = currentViewDist < 18.0 ? kShadowSamples
                       : currentViewDist < 45.0 ? 2 : 1;
    int shadowEdgeSamples = currentViewDist < 18.0 ? kShadowEdgeExtraSamples
                           : currentViewDist < 45.0 ? 2 : 0;
    int aoSamples = currentViewDist < 18.0 ? kAOSamples
                  : currentViewDist < 45.0 ? 1 : 0;
    int leafSamples = currentViewDist < 18.0 ? 4 : 2;
    bool shadowsEnabled = frame.windTime.z > 0.5;
    // Tree leaves in the mapped-shadow modes skip their per-pixel ray
    // queries entirely: canopy self-shadowing (the dominant lighting cue on
    // foliage) already comes from mappedTreeShadow below, and leaves are
    // visually noisy enough geometry that the remaining ray-only effects
    // (solid-occluder shadows from terrain/tank onto leaves, contact AO
    // inside a crown) don't read at gameplay distance. Measured ~3.5ms of an
    // 18ms foliage-lighting pass on the Arc A370M target. kTreeFoliage is a
    // specialization constant, so the main pipeline compiles this away and
    // every other material's rays are untouched; the legacy ray mode (F6)
    // also keeps the full ray path for comparison.
    bool foliageRaysSkipped = kTreeFoliage && frame.treeShadowParams.x > .5;
    // Skip the ray queries entirely rather than just discarding their
    // result -- the point of the toggle is to measure/avoid their cost, not
    // just their visual effect.
    // x: solid-occluder visibility. y: foliage transmission. Kept separate
    // rather than pre-multiplied -- see the temporal-blend comment below for
    // why one combined value can't be filtered with a single policy.
    vec2 rawShadowFoliage = shadowsEnabled && !foliageRaysSkipped
        ? traceSoftShadow(rayOrigin, toLight, kShadowTMax, noiseSeed, shadowSamples, shadowEdgeSamples,
                          leafSamples)
        : vec2(1.0);
    float rawSolid = rawShadowFoliage.x;
    float rawFoliage = rawShadowFoliage.y;
    // A different derived seed so AO's samples aren't identical to shadow's.
    float aoSeed = fract(noiseSeed * 2.718281828 + 0.31415926);
    float rawAO = shadowsEnabled && !foliageRaysSkipped && frame.treeShadowParams.w > .5 ? traceAO(rayOrigin, rayNormal, aoSeed, aoSamples, kAORadius, kAOStrength) : 1.0;

    // Temporal accumulation: blend this frame's noisy few-sample estimates
    // with history reprojected from last frame, so both terms converge
    // toward a stable, much-higher-effective-sample-count result over a
    // few frames instead of showing raw per-frame noise.
    //
    // The legacy ray mode retains the previous experimental accumulation.
    // Mapped tree visibility is evaluated after this block.
    // The solid-occluder and foliage-transmission terms are smoothed
    // independently (in separate history textures) rather than as one
    // combined number, because they need opposite blending policies. Solid
    // visibility legitimately needs the adaptive fast-snap-on-real-change
    // logic below (a moving tank's own shadow sweeping across static
    // ground). Foliage transmission has no equivalent fast case to protect:
    // the ray-traced leaf proxy is static geometry, decoupled from wind sway
    // (see basic.vert), so its true value at a given point never changes
    // frame to frame -- only the few-sample *estimate* of it does. Feeding
    // that estimate's noise through the same disagreement classifier as
    // solid visibility was the actual bug behind the reported "denoising
    // isn't working": a 2-4 ray estimate of a sparse, gappy proxy disagrees
    // with its own history by more than the dead zone on nearly every frame,
    // forever, since there's no real per-frame change for the noise to
    // settle down into agreeing with. That kept the adaptive alpha pinned
    // near its fast-snap ceiling for foliage-affected pixels permanently, so
    // raw sampling noise passed straight through basically every frame no
    // matter how the classifier's constants were tuned. Foliage instead
    // always uses a small fixed alpha (see kFoliageHistoryAlpha below):
    // heavy, unconditional smoothing is exactly correct for a value with
    // nothing legitimate to react quickly to.
    vec4 prevClip = frame.prevViewProj * vec4(fragPrevWorldPos, 1.0);
    // Motion vector for the TAA resolve, independent of whether the
    // shadow/AO reprojection below is enabled -- see computeScreenVelocity.
    outVelocity = computeScreenVelocity();
    float solidFactor = rawSolid;
    float foliageFactor = rawFoliage;
    float aoFactor = rawAO;
    float shadowDisagreementHistory = 0.0;
    // foliageRaysSkipped also skips the whole history read/blend: with the
    // raw estimates pinned at 1.0 there is nothing to accumulate, and the
    // reprojected history taps were a measurable slice of the foliage pass.
    if (shadowsEnabled && !foliageRaysSkipped && frame.treeShadowParams.z < .5 && prevClip.w > 0.001) {
        vec2 prevNDC = prevClip.xy / prevClip.w;
        // Y is flipped relative to the textbook NDC->UV formula because the
        // app renders with a negative-viewport-height trick (corrects
        // Vulkan's flipped-Y NDC for rasterization) -- that changes which
        // framebuffer/image row a given NDC.y lands on, so the UV mapping
        // has to flip to match, or this reads a vertically mirrored (and
        // therefore essentially unrelated) part of last frame's image.
        vec2 prevUV = vec2(prevNDC.x * 0.5 + 0.5, 0.5 - prevNDC.y * 0.5);
        if (prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0) {
            vec4 historySample = texture(historyShadow, prevUV);
            // Disocclusion check: if this exact world point HAD been
            // visible last frame, it should have measured this distance
            // from last frame's camera. Compare that to what was actually
            // stored at the reprojected pixel -- a large mismatch means a
            // different surface occupied that pixel last frame (e.g. the
            // tank has since moved away, revealing ground that used to be
            // hidden underneath it), so the stored value belongs to that
            // other surface and must not be blended in here. Shared by both
            // shadow and AO since they're read from the same pixel.
            float expectedPrevDist = length(frame.prevCameraPos.xyz - fragPrevWorldPos);
            float distDiff = abs(historySample.z - expectedPrevDist);
            float tolerance = max(0.05 * expectedPrevDist, 0.15);
            if (distDiff < tolerance) {
                float historyFoliageSample = texture(historyFoliage, prevUV).r;
                if (frame.treeShadowParams.x < .5) {
                    // Retain the previous recursive history filter only for
                    // the legacy comparison. Mapped tree shadows bypass it.
                    vec2 texelSize = 6.0 / vec2(textureSize(historyShadow, 0));
                    vec2 shadowAoSum = historySample.xy;
                    float tapWeight = 1.0;
                    // Foliage-transmission history shares these same four
                    // offsets and the same validity test (both textures are
                    // reprojected with the identical prevUV, so a tap that's
                    // across a depth edge for one is across it for the other).
                    float foliageSum = historyFoliageSample;
                    float foliageWeight = 1.0;
                    vec4 tapRight = texture(historyShadow, prevUV + vec2(texelSize.x, 0.0));
                    if (abs(tapRight.z - expectedPrevDist) < tolerance) {
                        shadowAoSum += tapRight.xy; tapWeight += 1.0;
                        foliageSum += texture(historyFoliage, prevUV + vec2(texelSize.x, 0.0)).r; foliageWeight += 1.0;
                    }
                    vec4 tapLeft = texture(historyShadow, prevUV - vec2(texelSize.x, 0.0));
                    if (abs(tapLeft.z - expectedPrevDist) < tolerance) {
                        shadowAoSum += tapLeft.xy; tapWeight += 1.0;
                        foliageSum += texture(historyFoliage, prevUV - vec2(texelSize.x, 0.0)).r; foliageWeight += 1.0;
                    }
                    vec4 tapUp = texture(historyShadow, prevUV + vec2(0.0, texelSize.y));
                    if (abs(tapUp.z - expectedPrevDist) < tolerance) {
                        shadowAoSum += tapUp.xy; tapWeight += 1.0;
                        foliageSum += texture(historyFoliage, prevUV + vec2(0.0, texelSize.y)).r; foliageWeight += 1.0;
                    }
                    vec4 tapDown = texture(historyShadow, prevUV - vec2(0.0, texelSize.y));
                    if (abs(tapDown.z - expectedPrevDist) < tolerance) {
                        shadowAoSum += tapDown.xy; tapWeight += 1.0;
                        foliageSum += texture(historyFoliage, prevUV - vec2(0.0, texelSize.y)).r; foliageWeight += 1.0;
                    }
                    historySample.xy = shadowAoSum / tapWeight;
                    historyFoliageSample = foliageSum / foliageWeight;
                }

                // Adaptive blend rate: the depth check only catches a
                // changed *surface* at this pixel, not a changed *lighting*
                // state on the same static surface -- e.g. ground the tank
                // has just driven off of is still the same ground (passes
                // the check above) but its true shadow/AO state just
                // flipped. A fixed slow blend would take many frames to
                // catch up, reading as a trailing smear following the
                // moving tank. Snap quickly (high alpha) when the fresh
                // estimate disagrees a lot with history; stay slow/stable
                // (low alpha) when they already roughly agree, to keep the
                // noise-smoothing benefit in the steady-state case.
                //
                // Dead zone below the ramp: with only a handful of samples
                // per frame, a penumbra pixel's rawSolid is quantized (5
                // samples => steps of 0.2) and jitters between those steps
                // every frame from sampling noise alone, not a real lighting
                // change. Without a dead zone that noise alone was enough to
                // disagree with history by more than the old multiplier
                // needed to hit max alpha, so penumbra pixels snapped hard
                // almost every frame and never actually accumulated -- the
                // literal cause of the reported "shadow edges still noisy,
                // not temporally stable".
                //
                // Threshold/floor tuned low: a hard occluder edge (e.g. the
                // tank's own shadow sweeping across static ground) is sharp
                // in world space, but the multi-sample jittered average
                // smooths that into a *gradual*, multi-frame ramp in the
                // estimate rather than one clean full-magnitude jump -- each
                // individual frame's disagreement during that transition was
                // often moderate, not large enough to clear the old
                // threshold (0.3), so it kept getting classified as noise
                // and heavily damped for the whole transition instead of
                // just the one frame it should've taken. Lowering the
                // threshold fixed the lag but let ordinary single-sample
                // quantization noise leak into the ramp too, since one noisy
                // frame alone can already exceed the lower bar.
                //
                // Fix: smooth the disagreement *signal* itself over 2 frames
                // before feeding it to the ramp, rather than reacting to a
                // single frame's value. A real transition's disagreement
                // stays elevated for several consecutive frames (that's what
                // the multi-frame ramp above described), so the smoothed
                // signal still climbs and triggers a fast blend within a
                // frame or so; a noise spike is a one-frame blip that gets
                // roughly halved away before it can trip the threshold.
                // The tank's own surface doesn't just need fast convergence
                // -- reprojecting it via prevViewProj is not even valid in
                // the first place. That reprojection assumes fragWorldPos
                // was a STATIC point that can be found in last frame's image
                // by undoing the camera's motion; for a moving rigid body,
                // this frame's world position of a given tank-local point
                // wasn't where that point was last frame (the tank was
                // somewhere else), so whatever pixel prevUV lands on holds
                // data for a different point entirely (or none). Blending
                // that in at any nonzero weight -- however small -- doesn't
                // just lag, it occasionally mixes in genuinely wrong data,
                // which is what kept showing up as ghosting no matter how
                // high the blend alpha went. Skip history for the tank's own
                // shadow entirely; isDynamicObject is a dedicated tag for
                // this rather than inferred from specularStrength (which
                // now varies between the tank's own camo/metal parts).
                bool isTank = isDynamicObject > 0.5;
                float shadowAlpha;
                if (isTank) {
                    shadowAlpha = 1.0;
                } else {
                    float shadowDisagreement = abs(rawSolid - historySample.x);
                    // Smoothed over more frames than the original 0.5:
                    // widening the 0.15 threshold itself to compensate for a
                    // noisier raw estimate was tried and rejected -- it
                    // biased the converged shadow noticeably brighter, not
                    // just slower to settle, because it suppresses response
                    // asymmetrically (rare bright sampling outliers still
                    // clear a higher bar and snap in fast; the more common
                    // moderate-dark readings no longer do). Averaging over
                    // more frames before comparing to the *same* original
                    // threshold instead asks for the disagreement to be
                    // sustained for longer, which quantization blips aren't
                    // and a real change (the tank driving under a tree, its
                    // own shadow sweeping past) still is.
                    shadowDisagreementHistory = mix(historySample.w, shadowDisagreement, 0.25);
                    shadowAlpha =
                        mix(0.08, 0.9, clamp((shadowDisagreementHistory - 0.15) * 4.0, 0.0, 1.0));
                }
                shadowAlpha = max(shadowAlpha, fragFoliageFade.w);
                solidFactor = mix(historySample.x, rawSolid, shadowAlpha);

                // AO: same invalid-reprojection reasoning as shadow above
                // applies here too, so skip history for the tank's own
                // surface entirely. Elsewhere, fixed fast blend: with a
                // fixed alpha, (1-alpha)^n of the stale value survives after
                // n frames -- at alpha=0.5 that's 12.5% still left after 3
                // frames, enough to read as a trail. alpha=0.75 leaves under
                // 2% after 3 frames, close enough to call fully resolved.
                float aoAlpha = isTank ? 1.0 : 0.75;
                aoAlpha = max(aoAlpha, fragFoliageFade.w);
                aoFactor = mix(historySample.y, rawAO, aoAlpha);

                // Foliage: no adaptive classifier, no isTank fast path, no
                // fragFoliageFade override -- unlike solid visibility and
                // AO, this value has no legitimate fast-changing case to
                // protect (see the comment above where solidFactor/
                // foliageFactor are declared), so it always uses the same
                // small alpha. (1-0.06)^n: about half the initial error is
                // gone after 11 frames, under 10% after 36 -- roughly a
                // second at 30fps to settle from a cold start, which is fine
                // for a value that then stays put.
                const float kFoliageHistoryAlpha = 0.06;
                foliageFactor = mix(historyFoliageSample, rawFoliage, kFoliageHistoryAlpha);
            }
        }
    }
    // Current-frame mapped visibility never enters screen-space history.
    if (frame.treeShadowParams.x > .5)
        foliageFactor = shadowsEnabled ? mappedTreeShadow(fragWorldPos, normal, toLight) : 1.0;
    if (frame.treeShadowParams.w < .5) aoFactor = 1.0;
    float shadowFactor = solidFactor * foliageFactor;
    // Applied after the temporal-history writes above (which carry only the
    // noisy ray-based terms): cloud shadow is smooth and deterministic, so
    // it needs no accumulation and must not contaminate the history's
    // disagreement classifier. Shares the F5 master shadows toggle.
    if (shadowsEnabled) shadowFactor *= cloudShadow(fragWorldPos);
    // A sentinel distance while shadows are off, not currentViewDist: re-
    // enabling shouldn't let the very next frame trust a "fully lit" history
    // written for a reason that had nothing to do with the actual surface.
    outShadowHistory = shadowsEnabled ? vec4(solidFactor, aoFactor, currentViewDist, shadowDisagreementHistory)
                                      : vec4(solidFactor, aoFactor, 50000.0, 0.0);
    outFoliageHistory = foliageFactor;

    // Diffuse-only bump: applied after the shadow/AO rays (which stay on the
    // true geometric normal -- perturbing their origin bias or hemisphere
    // basis with a fake micro-bump would just add noise, not detail) but
    // before the diffuse term, which is exactly where a flat-lit decal look
    // comes from.
    vec3 litNormal = normal;
    if (heightBlend > 0.5) {
        litNormal = normalize(normal - vec3(terrainBump.x, 0.0, terrainBump.y));
    } else if (bumpStrength > 0.001) {
        // Track marks: same idea as terrain's bump above (fake heightfield
        // perturbing the normal), but using trackHeightField's procedural
        // ridge signal instead of terrain's texture-luminance approach --
        // see trackHeightField's comment for why. Also, a track mark can be
        // rotated to any tank heading, so it can't take terrain's shortcut
        // of perturbing directly along world X/Z either. fragTangent (the
        // model matrix's local +X axis, matching the tread texture's U/width
        // direction -- see TrackTextureGenerator and Mesh::quad's UVs) gives
        // the real per-instance direction to perturb along instead.
        // Re-orthogonalize against the interpolated normal (Gram-Schmidt)
        // since fragTangent alone isn't guaranteed exactly perpendicular
        // after a non-uniform-scaled (width != length) model matrix.
        vec3 tangent = normalize(fragTangent - normal * dot(fragTangent, normal));
        vec3 bitangent = cross(tangent, normal);  // matches +V/forward, see fragTangent's comment
        const float kTrackBumpStep = 1.0 / 128.0;  // matches TrackTextureGenerator's size
        float heightCenter = trackHeightField(sampleUV);
        float heightU = trackHeightField(sampleUV + vec2(kTrackBumpStep, 0.0));
        float heightV = trackHeightField(sampleUV + vec2(0.0, kTrackBumpStep));
        vec2 trackBump = vec2(heightU - heightCenter, heightV - heightCenter) * bumpStrength;
        litNormal = normalize(normal - tangent * trackBump.x - bitangent * trackBump.y);
    }
    // Opaque foliage blobs, bark and rocks still need fine surface relief. Build a
    // derivative tangent frame from their real UV mapping, then treat albedo
    // luminance as a compact height channel. Rock is intentionally stronger.
    if ((materialType > 1.5 && materialType < 3.5) || barkMaterial) {
        vec3 dpdx = dFdx(fragWorldPos), dpdy = dFdy(fragWorldPos);
        vec2 duvdx = dFdx(sampleUV), duvdy = dFdy(sampleUV);
        float det = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
        if (abs(det) > 1e-6) {
            vec3 tangent = normalize((dpdx * duvdy.y - dpdy * duvdx.y) / det);
            vec3 bitangent = normalize((-dpdx * duvdy.x + dpdy * duvdx.x) / det);
            vec2 texel = 1.0 / vec2(textureSize(materialTexHighA, 0));
            vec3 lw = vec3(0.299, 0.587, 0.114);
            float h = dot(texColor, lw);
            float hu = dot(texture(materialTexHighA, sampleUV + vec2(texel.x, 0.0)).rgb, lw);
            float hv = dot(texture(materialTexHighA, sampleUV + vec2(0.0, texel.y)).rgb, lw);
            float strength = rockMaterial ? 7.0 : 2.5;
            litNormal = normalize(litNormal - tangent * (hu - h) * strength -
                                  bitangent * (hv - h) * strength);
        }
    }
    float diffuse = max(dot(litNormal, toLight), 0.0) * shadowFactor;
    // Ambient/fill term, darkened by AO at contact points (where the tank's
    // tracks, box bases, and tree trunks meet the ground) so those read as
    // grounded rather than floating; faces in shadow still read as dim
    // rather than pure black. Colored/directed by skyAmbientTint (see its
    // comment) instead of a flat constant, so a fragment's fill light now
    // visibly relates to the sky above it -- the same procedural sky the
    // dome, fog and reflections all already read from.
    vec3 ambientFill = skyAmbientTint(litNormal) * frame.ambientColor.w * aoFactor;
    vec3 lighting = ambientFill + frame.sunColor.rgb * frame.sunColor.w * diffuse;
    if (materialType > 1.5 && materialType < 2.5) {
        // Thin-leaf transmission: sunlight behind the surface produces a
        // warm green lift, while wrap lighting keeps solid canopy blobs from
        // developing unnaturally black hemispheres.
        float wrappedDiffuse = max((dot(litNormal, toLight) + 0.35) / 1.35, 0.0) * shadowFactor;
        float transmission = pow(max(dot(-litNormal, toLight), 0.0), 2.0) * shadowFactor;
        lighting = ambientFill +
                   frame.sunColor.rgb * frame.sunColor.w * (0.85 * wrappedDiffuse + 0.22 * transmission);
        albedo *= mix(vec3(0.88, 0.98, 0.82), vec3(1.08, 1.16, 0.72), transmission);
    }

    // Muzzle-flash/explosion point lights (see FrameUBO's dynamicLight*
    // arrays and DynamicLight.h) -- a simple unshadowed Lambertian
    // contribution per light, added on top of the sun-lit `lighting` above
    // rather than folded into it, since these are local and can be zero at
    // any given fragment (most of the time, all of them are). Deliberately
    // not ray-traced/shadowed: these are brief (<=0.3s) and few (<=4), so
    // the cost of real shadow rays isn't worth it for what's meant to read
    // as a quick flash, not a precise light source. Gated by aoFactor for
    // the same reason the Fresnel term below is: without it, a flash next
    // to a deep contact-AO crevice (e.g. under the tank) lights the inside
    // of that crevice as if it weren't occluded at all.
    vec3 dynamicLight = vec3(0.0);
    for (int i = 0; i < MAX_DYNAMIC_LIGHTS; ++i) {
        float lightIntensity = frame.dynamicLightColorIntensity[i].w;
        if (lightIntensity <= 0.0) continue;
        vec3 lightPos = frame.dynamicLightPosRadius[i].xyz;
        float lightRadius = frame.dynamicLightPosRadius[i].w;
        vec3 toDynLight = lightPos - fragWorldPos;
        float dynDist = length(toDynLight);
        if (dynDist >= lightRadius) continue;
        vec3 dynLightDir = toDynLight / max(dynDist, 0.001);
        float dynNdotL = max(dot(litNormal, dynLightDir), 0.0);
        // Smooth falloff to exactly 0 at lightRadius (squared so most of
        // the falloff happens near the edge, roughly inverse-square-ish
        // close to the light) -- avoids a hard-edged circle of light
        // sweeping across the ground as the flash's radius shrinks with it.
        float dynFalloff = 1.0 - dynDist / lightRadius;
        dynFalloff *= dynFalloff;
        dynamicLight += frame.dynamicLightColorIntensity[i].rgb * lightIntensity * dynNdotL * dynFalloff;
    }
    dynamicLight *= aoFactor;

    // Per-pixel specular map for the tank: tankSpecularGrain gives a fine,
    // scratched-metal-like shimmer across the whole surface -- pure noise,
    // no regular/periodic structure, unlike an earlier version of this that
    // also perturbed the normal in a seam grid (removed: at any grid
    // spacing fine enough to read as detail up close, it read as an
    // obviously artificial checkerboard at normal viewing distance instead
    // of like paneling). Left as plain pc.specularStrength for everything
    // else (terrain, water, etc.), same as before.
    float specularStrength = pc.specularStrength;
    if (isDynamicObject > 0.5 && !tankMaterial) {
        specularStrength = pc.specularStrength * mix(0.5, 1.5, tankSpecularGrain(fragUV));
    }
    if (tankMaterial) {
        specularStrength = pc.specularStrength * mix(0.75, 1.25, tankGrain);
        specularStrength *= 1.0 - clamp(tankDust * 1.7 + tankSoot * 0.9, 0.0, 0.9);
    }
    // Stone has a broad, faint mineral response; foliage only a tiny waxy
    // sheen. Both remain much rougher than painted metal.
    if (materialType > 2.5 && materialType < 3.5) specularStrength = 0.055;
    else if (barkMaterial) specularStrength = 0.01;
    else if (materialType > 1.5 && materialType < 2.5) specularStrength = 0.025;

    vec3 viewDir = normalize(frame.cameraPos.xyz - fragWorldPos);

    // Animated ripple: perturbs a *separate* shading normal used only by
    // the specular/Fresnel/reflection terms below, not the real diffuse
    // term or shadow-ray direction above -- water's surface should look
    // rippled without actually changing how it's lit/shadowed (which
    // would require real geometric displacement to do correctly). Two
    // criss-crossing sine waves at different scales/speeds avoid an
    // obviously repeating single-wave look; cameraPos.w is the same
    // per-frame counter already reused for shadow/AO jitter, just repurposed
    // here as an animation phase.
    vec3 shadingNormal = normal;
    // Wave-crest foam coverage, accumulated from the interactive wave
    // sources below and applied to the final surface colour/alpha.
    float waterFoam = 0.0;
    bool isWater = waveStrength > 0.001;
    if (isWater) {
        float t = frame.cameraPos.w;
        // Water UVs carry the generated per-vertex flow direction (water
        // binds the plain white texture, so they are otherwise unused).
        // Still water (zero flow: lakes, the legacy flooded basins) keeps
        // the original time-phased criss-cross shimmer; moving water
        // advects that same pattern downstream instead. The drift uses two
        // half-offset sawtooth phases cross-faded flow-map style, so it
        // stays continuous across cameraPos.w's 1024-frame wrap.
        vec2 flow = fragUV;
        float moving = clamp(length(flow), 0.0, 1.0);
        float still = 1.0 - moving;
        float cycle = t * (8.0 / 1024.0);
        float phase0 = fract(cycle), phase1 = fract(cycle + 0.5);
        float blend = abs(phase0 * 2.0 - 1.0);
        vec2 p0 = fragWorldPos.xz - flow * (phase0 * 1.5);
        vec2 p1 = fragWorldPos.xz - flow * (phase1 * 1.5);
        float s1 = t * 0.035 * still, s2 = t * 0.025 * still;
        float s3 = t * 0.065 * still, s4 = t * 0.045 * still;
        float wave1 = mix(sin(p0.x * 1.3 + s1) * cos(p0.y * 1.7 - s2),
                          sin(p1.x * 1.3 + s1) * cos(p1.y * 1.7 - s2), blend);
        float wave2 = mix(sin(p0.x * 3.1 - s3 + 1.7) * cos(p0.y * 2.3 + s4),
                          sin(p1.x * 3.1 - s3 + 1.7) * cos(p1.y * 2.3 + s4), blend);
        // Moving water is choppier than a still pond.
        vec2 bump = vec2(wave1, wave2) * waveStrength * (1.0 + moving * 0.6);
        // Distant ripple wavelengths fall below a pixel and alias into a
        // honeycomb lattice in grazing cloud reflections (visible on any
        // large lake); fade toward calm water with distance instead. The
        // small floor keeps a hint of sparkle without the full pattern.
        bump *= mix(1.0, 0.1, smoothstep(18.0, 50.0, currentViewDist));
        // Interactive waves (shell splashes, the tank's wading wake) -- see
        // frame.waterWaves. Each live source is an expanding ring wave
        // packet: a few sinusoidal crests inside an envelope riding the
        // advancing front, sharp ahead of it and trailing a longer damped
        // tail behind, exactly how a real disturbance on a calm pond decays.
        // Summed as radial slopes into the same shading normal as the
        // ambient shimmer above, so reflections and the sun glint visibly
        // bend around each crest as it propagates.
        vec2 waveBump = vec2(0.0);
        for (int i = 0; i < MAX_WATER_WAVES; ++i) {
            float slopeAmp = frame.waterWaves[i].w;
            if (slopeAmp <= 0.0) continue;
            vec2 toFrag = fragWorldPos.xz - frame.waterWaves[i].xy;
            float d = length(toFrag);
            // Signed distance from the wavefront; outside the packet the
            // envelope is ~0, skip before the transcendentals.
            float u = d - frame.waterWaves[i].z;
            if (u > 2.0 || u < -4.5 || d < 1e-4) continue;
            float envelope = exp(-u * u * (u > 0.0 ? 1.4 : 0.35));
            // Wavelength ~1 unit -- large enough to survive TAA at
            // mid-distance, small enough to read as water, not swell.
            waveBump += (toFrag / d) * (slopeAmp * 1.4 * envelope * cos(u * 6.4));
            // Foam scales with slope SQUARED: only steep young crests churn
            // white, while an aging swell keeps bending reflections long
            // after its foam has dissolved -- exactly how a real splash
            // fades. The much tighter envelope keeps the trace a narrow band
            // hugging the front, not a filled disc.
            waterFoam += slopeAmp * slopeAmp * exp(-u * u * (u > 0.0 ? 6.0 : 2.0));
        }
        // Same aliasing guard as the ambient shimmer, but the ~1-unit
        // wavelength holds up much farther before dropping below a pixel.
        bump += waveBump * mix(1.0, 0.15, smoothstep(30.0, 80.0, currentViewDist));
        // Broken, patchy foam: modulate hard by the criss-cross shimmer
        // already computed above so crests read as churned bubbles rather
        // than the solid painted rings the old billboard meshes drew.
        waterFoam = clamp(waterFoam * 5.0, 0.0, 0.6) *
                    (0.35 + 0.65 * clamp(0.5 + wave1 * wave2 * 2.5, 0.0, 1.0));
        shadingNormal = normalize(normal + vec3(bump.x, 0.0, bump.y));
        // A real sun-glint on water is a small, tight, bright highlight, not
        // a broad sheen -- low roughness gives GGX the same tight-highlight
        // behavior the old exponent-150 Blinn-Phong term aimed for.
        roughness = 0.06; metalness = 0.0; f0Dielectric = 0.02;
    }

    // Energy-conserving GGX microfacet specular replaces the old flat
    // Blinn-Phong highlight for every opaque material. Leaves keep their
    // existing Blinn-Phong-lite look untouched -- an explicit compatibility
    // case (see the roadmap), not covered by this opaque-surface model.
    // diffuseWeight is the BRDF's own (1-Fresnel)*(1-metalness) energy
    // split; for leaves it instead reproduces the old flat
    // mix(1.0,0.75,specularStrength) diffuse-darkening constant exactly, so
    // foliage stays pixel-identical.
    vec3 specular;
    vec3 diffuseWeight;
    float fresnelRim = 0.0;
    // Ambient light reflected specularly rather than diffusely, filling in
    // exactly the energy diffuseWeight removes below for metals. Without
    // this, a high-metalness surface (diffuseWeight collapses toward 0) is
    // only ever lit by the sun's tight GGX highlight, reading as near-black
    // everywhere else. Most materials use a broad normal-based sky fill;
    // the tank adds a view-dependent sky/ground approximation below. A real
    // prefiltered environment/irradiance pass remains later roadmap work.
    // F0 tints this by the steel colour for metals and by the small
    // dielectric reflectance otherwise. Leaves keep their existing model.
    vec3 ambientSpecular = vec3(0.0);
    if (isLeaf) {
        vec3 halfDir = normalize(toLight + viewDir);
        float specAngle = max(dot(shadingNormal, halfDir), 0.0);
        specular = vec3(pow(specAngle, 20.0) * specularStrength * 0.6 * shadowFactor);
        // Fresnel/rim term: surfaces brighten at grazing view angles, a
        // cheap but very characteristic cue for metal. Kept subtle and
        // tinted toward neutral gray rather than white so it doesn't bleach
        // the paint color. Gated by aoFactor (unlike specular above, which
        // already has shadowFactor) -- grazing angles cluster inside
        // concave nooks exactly where AO is darkest, so without this an
        // occluded crevice still gets a full-strength rim glow that reads
        // as a lit patch floating in shadow.
        fresnelRim = pow(1.0 - max(dot(shadingNormal, viewDir), 0.0), 3.0) *
                     specularStrength * 0.18 * aoFactor;
        diffuseWeight = vec3(mix(1.0, 0.75, specularStrength));
    } else {
        vec3 f0 = mix(vec3(f0Dielectric), metalTint, metalness);
        BrdfResult brdf = evaluateGGX(shadingNormal, viewDir, toLight, roughness, metalness, f0);
        specular = brdf.specular * specularStrength * shadowFactor;
        diffuseWeight = brdf.diffuseWeight;
        // Grazing-angle brightening is already inside the BRDF's own
        // Fresnel term above -- no separate rim term needed.
        ambientSpecular = skyAmbientTint(shadingNormal) * frame.ambientColor.w * aoFactor *
                          f0 * mix(1.0, 0.5, roughness);
        if (tankMaterial) {
            // A broad sky/ground reflection gives armour a moving sheen
            // even away from the direct sun highlight. Approximate the
            // rough environment analytically, without extra reflection rays.
            vec3 reflected = reflect(-viewDir, shadingNormal);
            vec3 groundFill = frame.ambientColor.rgb * vec3(0.32, 0.29, 0.23);
            vec3 environment = mix(groundFill, skyGradient(reflected),
                                   smoothstep(-0.25, 0.35, reflected.y));
            environment = mix(environment, skyAmbientTint(shadingNormal), roughness * roughness);
            float grazing = pow(1.0 - max(dot(shadingNormal, viewDir), 0.0), 5.0);
            vec3 envFresnel = f0 + (max(vec3(1.0 - roughness), f0) - f0) * grazing;
            ambientSpecular = environment * frame.ambientColor.w * aoFactor *
                              envFresnel * mix(1.0, 0.5, roughness);
        }
    }

    vec3 base = albedo * lighting * diffuseWeight + ambientSpecular + albedo * dynamicLight;

    // Environment reflection. Base case is the analytic sky+cloud function
    // above sampled along the reflection vector -- one shared cloud lookup,
    // and correct for the common case of a reflection heading toward open
    // sky (matches what the actual sky dome looks like, since the dome
    // itself is deliberately kept out of the ray-traced scene). Reflective
    // surfaces (reflectivity > 0: the tank, water) additionally trace an
    // actual ray along reflectDir; a hit replaces the sky color with a
    // real, occlusion-aware shaded result (see traceReflection) so nearby
    // trees/rocks/terrain darken the reflection instead of it always
    // showing sky regardless of what's actually nearby. Gated behind
    // reflectivity so matte surfaces (terrain, trees, boxes -- the vast
    // majority of fragments) never pay for a ray they'd multiply by zero
    // anyway. Uses shadingNormal (the rippled one for water) so the
    // reflection direction wobbles with the fake waves too.
    // Water specifically: real water's reflectivity and transparency are
    // both strongly view-angle dependent (Fresnel) -- near-mirror at
    // grazing angles, mostly see-through when looking straight down into
    // it. A flat reflectivity/opacity made it look like tinted plastic
    // rather than water. Schlick's approximation with F0 ~ water's real
    // ~0.02-0.03 normal-incidence reflectance drives both terms together:
    // grazing views read as a reflective sheet (reflectivity and alpha
    // both push toward 1), steep/overhead views let the lake bed and its
    // own duller color show through -- except in deep water, see
    // waterDepthT below, which overrides that see-through case: real deep
    // water absorbs/scatters away the light that would otherwise reach the
    // bottom and return, so you don't see the lakebed there regardless of
    // viewing angle.
    float effectiveReflectivity = reflectivity;
    // Recovers the depth fraction WaterGenerator.cpp baked into fragColor
    // (mix(shallowColor, deepColor, depthT), see buildMesh) by projecting
    // back onto that known line -- avoids needing a dedicated depth vertex
    // attribute just for this. Must track WaterGenerator.cpp's palette.
    const vec3 kWaterShallowColor = vec3(0.085, 0.125, 0.075);
    const vec3 kWaterDeepColor = vec3(0.012, 0.022, 0.018);
    float waterDepthT = 0.0;
    if (isWater) {
        vec3 span = kWaterDeepColor - kWaterShallowColor;
        waterDepthT = clamp(dot(fragColor - kWaterShallowColor, span) / dot(span, span), 0.0, 1.0);

        float cosTheta = clamp(dot(shadingNormal, viewDir), 0.0, 1.0);
        // A steeper falloff (exponent 8, not the textbook Schlick 5) so a
        // typical chase-cam view of a mid-distance pond -- which sees it at
        // a fairly shallow angle simply from being farther away horizontally
        // than the camera is elevated above it, without being anywhere near
        // truly grazing -- stays mostly in the low-reflectivity, transparent
        // regime instead of already reading as a half-mirrored sky sheet.
        // Also capped further below 1 than a literal mirror even at the most
        // grazing angles, so the tinted/depth-darkened water color
        // (baseContribution below) always shows through at least a little.
        float waterFresnel = mix(0.03, 0.45, pow(1.0 - cosTheta, 8.0));
        effectiveReflectivity = mix(reflectivity * 0.25, 0.45, waterFresnel);
        // Fresnel alone floors alpha low for a straight-down view regardless
        // of depth, which reads as "always see the bottom" -- fine for a
        // shallow puddle, wrong for a deep lake. depthAlphaFloor raises that
        // floor with depth so deep water stays substantially opaque even
        // overhead; Fresnel can still push it higher at grazing angles on
        // top of that. Deep end lowered from 0.95 -- fully opaque dark water
        // combined with any reflection blend read as a milky/hazy film
        // rather than dark, clear, and just slightly reflective.
        // Shallow floor raised from 0.18: with the bed that visible, a pond
        // over grey rock read as a sheet of mercury rather than water with
        // its own body of colour.
        float depthAlphaFloor = mix(0.3, 0.78, waterDepthT);
        finalAlpha = max(depthAlphaFloor, mix(pc.opacity * 0.4, 0.6, waterFresnel));
    } else {
        // Rough surfaces reflect the environment more weakly/diffusely than
        // a mirror-smooth one -- a cheap analytic stand-in for real
        // roughness-filtered environment sampling. Still just the existing
        // unfiltered procedural-sky/ray-traced sample below; a full
        // prefiltered-IBL pass is later roadmap work.
        effectiveReflectivity *= mix(1.0, 0.15, roughness);
    }

    // Matte surfaces do not need an environment color at all. Keeping the
    // analytic cloud sky and reflection direction inside this uniform branch
    // avoids a sizeable block of procedural noise work on terrain, foliage,
    // rocks, crates, and other non-reflective draws without changing output.
    vec3 envColor = vec3(0.0);
    if (effectiveReflectivity > 0.01) {
        vec3 reflectDir = reflect(-viewDir, shadingNormal);
        // Roughness-filtered environment sample: a mirror-smooth surface
        // (roughness 0) samples the sky exactly along the reflection vector;
        // a rougher surface blends toward skyAmbientTint(shadingNormal) --
        // the same sky-derived hemisphere tint diffuse/ambientSpecular
        // lighting above already uses, and the direction a fully rough
        // "reflection" actually converges to (every incoming direction
        // scattered roughly evenly across the surface's local hemisphere).
        // There's no real prefiltered mip chain here (the sky is a
        // procedural function, not a captured cubemap to blur), so this is
        // the cheap analytic equivalent, not a full IBL pass -- see the
        // roadmap's "Selective advanced lighting and atmosphere" entry.
        vec3 sharpEnv = skyColor(reflectDir);
        vec3 roughEnv = skyAmbientTint(shadingNormal);
        envColor = mix(sharpEnv, roughEnv, roughness);
        vec3 reflectionHit;
        vec3 reflOrigin = fragWorldPos + normal * kShadowBias;
        // Independent of shadow/AO toggles. Keep the sky fallback when F9
        // disables closest-hit rays, isolating reflected-geometry cost.
        if (frame.windTime.w > .5 && traceReflection(reflOrigin, reflectDir, kShadowTMax, reflectionHit)) {
            envColor = mix(reflectionHit, roughEnv, roughness);
        }
    }

    // Extra absorption beyond the deepColor tint itself: water reads as
    // genuinely darker than dry lit ground even at its shallowest (light
    // scattered/absorbed within any water column, however thin), getting
    // properly close to black at the deepest points of a basin -- starting
    // this mix at 1.0 (no darkening at all in shallow water) was what left
    // most of a shallow pond looking barely different from, and about as
    // bright as, the grass around it.
    vec3 baseContribution = isWater ? base * mix(0.4, 0.05, waterDepthT) : base;
    // A true blend rather than adding the reflection on top of the full
    // base color -- the previous `base + env*reflectivity` double-counts
    // brightness (at reflectivity 0.4 you'd get 100% of base AND 40% of a
    // bright sky color, reading as a washed-out pale sheet rather than
    // "mostly transparent, tinted by depth, plus a reflection"). Negligible
    // difference for the tank's own tiny reflectivity (0.06).
    vec3 result = mix(baseContribution, envColor, effectiveReflectivity) +
                  specular * frame.sunColor.rgb * frame.sunColor.w + fresnelRim * vec3(0.6);

    // Wave-crest foam sits on top of the reflection/absorption blend:
    // aerated water scatters diffusely white and hides the surface beneath,
    // so it also pulls alpha up toward opaque where it is dense.
    if (waterFoam > 0.001) {
        result = mix(result, vec3(0.72, 0.78, 0.8), waterFoam);
        finalAlpha = mix(finalAlpha, 0.92, waterFoam);
    }

    // Fogged toward the sky color along the actual camera->fragment
    // direction (not the reflection vector envColor uses above) so it reads
    // as haze sitting between the viewer and the surface, not a reflection.
    // currentViewDist is already computed above for the temporal-history
    // disocclusion check.
    vec3 viewToFragDir = normalize(fragWorldPos - frame.cameraPos.xyz);
    vec3 fogColor = skyGradient(viewToFragDir);
    float fogDist = max(currentViewDist - frame.atmosphere.x, 0.0);
    float fogFactor = 1.0 - exp(-fogDist * frame.atmosphere.y);

    // Aerial perspective, separate from (and starting before) the fog term:
    // real haze mutes saturation and cool-shifts colour long before it
    // whites anything out. With only the exponential fog above, mid-distance
    // trees stayed fully saturated and then hills abruptly went milky --
    // there was no gradual tonal recession tying foreground to horizon.
    // Ramps in from just past the chase-cam range and saturates at ~50%
    // desaturation so even the far edge keeps some of its own colour; the
    // fog mix below then carries the final fade into the sky. Skips the sky
    // and unlit (boundary-line) paths, which returned earlier.
    const float kAerialStart = 25.0;
    const float kAerialDensity = 0.0045;
    const float kAerialMax = 0.5;
    float aerialT = (1.0 - exp(-max(currentViewDist - kAerialStart, 0.0) * kAerialDensity)) * kAerialMax;
    float resultLuma = dot(result, vec3(0.2126, 0.7152, 0.0722));
    result = mix(result, vec3(resultLuma) * vec3(0.95, 0.99, 1.06), aerialT);

    result = mix(result, fogColor, fogFactor);

    outColor = vec4(result, finalAlpha);
}
