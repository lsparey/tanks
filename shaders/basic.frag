#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_ray_tracing_position_fetch : require

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragTangent;

// Must match DynamicLight.h's kMaxDynamicLights -- GLSL can't share that
// constant with the C++ side, so the array size here is a plain literal.
#define MAX_DYNAMIC_LIGHTS 4

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 prevViewProj;
    vec4 lightDir;   // direction the light travels, xyz
    vec4 cameraPos;
    vec4 prevCameraPos;
    // Muzzle-flash/explosion point lights -- see DynamicLight.h and
    // Application::drawFrame, which fills these each frame. xyz position,
    // w radius; rgb color, w intensity. A radius/intensity of 0 (the
    // default for any slot beyond however many lights are actually live)
    // means "inactive, skip" -- see the loop in main().
    vec4 dynamicLightPosRadius[MAX_DYNAMIC_LIGHTS];
    vec4 dynamicLightColorIntensity[MAX_DYNAMIC_LIGHTS];
} frame;

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
layout(set = 2, binding = 0) uniform accelerationStructureEXT sceneTLAS;
layout(set = 3, binding = 0) uniform sampler2D historyShadow;

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
} pc;

layout(location = 0) out vec4 outColor;
// x: temporally-blended shadow factor. y: temporally-blended AO factor.
// z: view-distance at write time, used next frame to detect disocclusion
// (see main()). w: 2-frame-smoothed shadow disagreement, used next frame to
// tell a sustained real change from a one-frame noise spike (see main()).
layout(location = 1) out vec4 outShadowHistory;

// Hard visibility test via a single ray query: 1.0 if nothing occludes the
// path from `origin` toward `direction`, 0.0 if something does.
// TerminateOnFirstHit since this is a boolean visibility test, not a
// closest-hit lookup -- any hit at all means occluded.
float traceShadow(vec3 origin, vec3 direction, float tMax) {
    rayQueryEXT rayQuery;
    rayQueryInitializeEXT(rayQuery, sceneTLAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
                           0xFF, origin, 0.001, direction, tMax);
    while (rayQueryProceedEXT(rayQuery)) {}
    return rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT
               ? 1.0
               : 0.0;
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
    rayQueryInitializeEXT(rayQuery, sceneTLAS, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.001, direction,
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
    hitColor = mix(vec3(0.05), vec3(0.5), diffuse);
    return true;
}

// Cheap, texture-free per-pixel pseudo-random value for jittering shadow/AO
// rays without a fixed sampling pattern (which would band/tile visibly).
float interleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// A from-scratch (not texture-sampled) value-noise fbm, independent of
// CloudTextureGenerator's tileable version -- this one is evaluated
// continuously per-pixel for an arbitrary direction, so it has no need for
// (and doesn't bother with) exact seamless tiling.
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

float cloudFbm(vec2 p) {
    float sum = 0.0;
    float amplitude = 0.5;
    float total = 0.0;
    for (int i = 0; i < 4; ++i) {
        sum += amplitude * valueNoise2D(p);
        total += amplitude;
        p *= 2.0;
        amplitude *= 0.5;
    }
    return sum / total;
}

// Plain two-tone sky gradient, no clouds -- factored out of skyColor below
// so distance fog (which tints toward this per-fragment, at every fog-
// affected pixel on screen) can use just the smooth gradient. Using the
// full cloud-textured skyColor there made the cloud pattern visibly bleed
// onto nearby opaque geometry (tree trunks, rocks) any time that fragment's
// camera-to-surface direction pointed even slightly upward, since fog
// blends in this color starting close to the camera.
vec3 skyGradient(vec3 dir) {
    vec3 skyTint = vec3(0.55, 0.65, 0.78);
    vec3 groundTint = vec3(0.12, 0.11, 0.10);
    return mix(groundTint, skyTint, clamp(dir.y * 0.5 + 0.5, 0.0, 1.0));
}

// Analytic sky (two-tone gradient) + clouds for an arbitrary view/reflection
// direction, evaluated directly rather than sampled from the actual sky
// dome's texture -- the dome is deliberately kept out of the ray-traced
// scene (see gatherRayTracingInstances), so a ray that escapes to open sky
// has nothing to hit; this gives reflections (see traceReflection's miss
// case) a plausible cloud-textured sky instead of a flat gradient without
// needing the dome itself to be ray-traceable. Won't look pixel-identical
// to the actual rendered sky dome (different noise implementation/
// parameters), but reads as the same kind of sky.
vec3 skyColor(vec3 dir) {
    vec3 color = skyGradient(dir);
    if (dir.y > 0.02) {
        // Project onto a distant horizontal plane, same idea as the sky
        // dome mesh's own UV mapping (see Mesh::dome).
        vec2 p = dir.xz / dir.y;
        float density = cloudFbm(p * 0.5) * 0.75 + cloudFbm(p * 2.0 + vec2(41.3, 7.1)) * 0.25;
        float cloudAlpha = smoothstep(0.52, 0.70, density);
        vec3 cloudColor = mix(vec3(1.0), vec3(0.72, 0.75, 0.80), smoothstep(0.6, 0.9, density));
        color = mix(color, cloudColor, cloudAlpha);
    }
    return color;
}

// Reimplements TrackTextureGenerator's tread-link ridge pattern as a
// procedural [0,1] height field (not sampled from the actual texture) --
// same idea as cloudFbm's independent reimplementation of
// CloudTextureGenerator above. Needed because the real texture's own
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

float traceSoftShadow(vec3 origin, vec3 lightDir, float tMax, float noiseSeed) {
    vec3 t, b;
    buildBasis(lightDir, t, b);

    float sum = 0.0;
    for (int i = 0; i < kShadowSamples; ++i) {
        float u1 = fract(noiseSeed + float(i) * 0.6180339887);   // golden-ratio jitter
        float u2 = fract(noiseSeed * 1.618 + float(i) * 0.3819660113);
        float angle = u1 * 6.2831853;
        float radius = kConeAngle * sqrt(u2);
        vec3 jitteredDir = normalize(lightDir + radius * (cos(angle) * t + sin(angle) * b));
        sum += traceShadow(origin, jitteredDir, tMax);
    }

    float coarseAvg = sum / float(kShadowSamples);
    if (coarseAvg < 0.001 || coarseAvg > 0.999) return coarseAvg;

    int totalSamples = kShadowSamples + kShadowEdgeExtraSamples;
    for (int i = kShadowSamples; i < totalSamples; ++i) {
        float u1 = fract(noiseSeed + float(i) * 0.6180339887);
        float u2 = fract(noiseSeed * 1.618 + float(i) * 0.3819660113);
        float angle = u1 * 6.2831853;
        float radius = kConeAngle * sqrt(u2);
        vec3 jitteredDir = normalize(lightDir + radius * (cos(angle) * t + sin(angle) * b));
        sum += traceShadow(origin, jitteredDir, tMax);
    }
    return sum / float(totalSamples);
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

float traceAO(vec3 origin, vec3 normal, float seedBase) {
    float occlusion = 0.0;
    for (int i = 0; i < kAOSamples; ++i) {
        // Different irrational offsets than the shadow jitter's, so the two
        // don't end up correlated (same seedBase, different sample set).
        float u1 = fract(seedBase + float(i) * 0.7548776662);
        float u2 = fract(seedBase * 1.3247179572 + float(i) * 0.5698402910);
        vec3 sampleDir = cosineSampleHemisphere(normal, u1, u2);
        occlusion += 1.0 - traceShadow(origin, sampleDir, kAORadius);
    }
    return 1.0 - kAOStrength * (occlusion / float(kAOSamples));
}

// The swapchain's attachment format is sRGB (see Swapchain::imageFormat_),
// so the driver auto-encodes whatever linear color this shader writes --
// but it does so straight onto an 8-bit target, meaning anything above 1.0
// (the sun-glint specular term especially, exponent 150 on water) simply
// clips to flat white with a hard edge. Compressing through a filmic curve
// first gives those highlights a smooth rolloff instead, and pulls the
// whole image's contrast a little closer to how a camera/eye actually
// responds rather than the linear-clip default.
const float kExposure = 1.0;

// Narkowicz 2015 ACES filmic fit -- a widely-used cheap approximation of
// the full ACES tonemap curve, accurate enough for this purpose without
// needing the real curve's 3D LUT.
vec3 acesFilmicTonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Exponential distance fog, tinted by the plain sky gradient (not
// skyColor's cloud-textured version -- see skyGradient's comment) -- gives
// the terrain's ~255-unit corner-to-corner diagonal (see Camera::
// projection's far plane) a depth cue and hazes the far edge toward the
// horizon instead of it staying full-contrast right up to the view's far
// clip. kFogStartDistance keeps the near/gameplay range (chase cam sits
// ~8 units back, see Camera::followTarget) completely clear so fog only
// ever shows up well beyond the action; density is applied to distance
// past that start, not total distance, so it ramps in gradually rather
// than jumping straight to its far-clip value at the start line.
const float kFogStartDistance = 60.0;
const float kFogDensity = 0.004;

void main() {
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
    if (pc.heightBlend > 0.5) {
        vec2 warp = vec2(valueNoise2D(fragWorldPos.xz * 0.015 + vec2(5.2, 88.1)),
                          valueNoise2D(fragWorldPos.xz * 0.017 + vec2(41.7, 12.3)));
        sampleUV += (warp - 0.5) * 0.6;
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
    if (pc.heightBlend > 0.5) {
        // Terrain: within each zone (grass, gravel), patch-blend between two
        // texture variants using a large-scale noise mask, so the ground
        // reads as naturally varied -- patches of lusher/drier grass,
        // lighter/darker gravel -- instead of one texture repeated
        // everywhere. Two octaves per zone for a less obviously-round patch
        // shape; independent noise (different frequency/offset) per zone so
        // grass patches and gravel patches don't line up with each other or
        // with the height-based zone boundary below.
        float grassPatch = valueNoise2D(fragWorldPos.xz * 0.06 + vec2(19.3, 4.7)) * 0.7 +
                            valueNoise2D(fragWorldPos.xz * 0.15 + vec2(58.1, 91.4)) * 0.3;
        float gravelPatch = valueNoise2D(fragWorldPos.xz * 0.08 + vec2(71.2, 33.6)) * 0.7 +
                             valueNoise2D(fragWorldPos.xz * 0.2 + vec2(12.9, 47.5)) * 0.3;
        vec3 grassColor =
            mix(texColor, texture(materialTexHighB, sampleUV).rgb, smoothstep(0.4, 0.6, grassPatch));
        vec3 gravelColor = mix(texture(materialTexLowA, sampleUV).rgb, texture(materialTexLowB, sampleUV).rgb,
                                smoothstep(0.4, 0.6, gravelPatch));

        // Fade to the low-point (gravel) blend in valleys. Center threshold
        // tuned against the heightmap's actual range (roughly -3..-5.5 on
        // the low end, seed-dependent, now that HeightmapGenerator layers a
        // plateau and a steepest-descent-traced river valley on top of the
        // base rolling hills, versus the plain +-3 of the old hills-only
        // version -- see HeightmapGenerator.cpp). The
        // threshold itself is jittered by a low-frequency noise
        // (independent of grassPatch/gravelPatch above, different
        // frequency/offset so it doesn't line up with either) rather than
        // being a pure function of height -- a plain height threshold draws
        // a smooth iso-height contour line around every hill, which reads
        // as an artificial gradient band running exactly level around the
        // terrain; jittering it makes the boundary wander like an actual
        // patchy transition instead.
        float rockyThreshold =
            -2.3 + (valueNoise2D(fragWorldPos.xz * 0.05 + vec2(153.2, 88.7)) - 0.5) * 1.4;
        float rockiness = 1.0 - smoothstep(rockyThreshold - 0.4, rockyThreshold + 0.4, fragWorldPos.y);

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
        float steepness = 1.0 - normalize(fragNormal).y;
        float slopeRockiness = smoothstep(0.45, 0.78, steepness);
        rockiness = max(rockiness, slopeRockiness);
        texColor = mix(grassColor, gravelColor, rockiness);

        const float kTerrainBumpTexelStep = 1.0 / 512.0;  // matches kTerrainTextureRes in Application.cpp
        const float kTerrainBumpStrength = 3.0;
        vec3 luminanceWeights = vec3(0.299, 0.587, 0.114);
        float heightCenter = dot(texture(materialTexHighA, sampleUV).rgb, luminanceWeights);
        float heightU = dot(texture(materialTexHighA, sampleUV + vec2(kTerrainBumpTexelStep, 0.0)).rgb,
                             luminanceWeights);
        float heightV = dot(texture(materialTexHighA, sampleUV + vec2(0.0, kTerrainBumpTexelStep)).rgb,
                             luminanceWeights);
        terrainBump = vec2(heightU - heightCenter, heightV - heightCenter) * kTerrainBumpStrength;
    }
    vec3 albedo = fragColor * texColor;
    // Texture alpha times the per-draw opacity (PushConstants::opacity) --
    // both are 1.0 for every opaque draw in the scene, so this only actually
    // does something for fading ground decals like TrackMark, whose texture
    // has a soft alpha falloff and whose opacity decreases as it ages.
    float finalAlpha = texSample.a * pc.opacity;

    if (pc.unlit > 0.5) {
        outColor = vec4(acesFilmicTonemap(albedo * kExposure), finalAlpha);
        outShadowHistory = vec4(1.0, 1.0, 50000.0, 0.0);
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
    vec3 rayOrigin = fragWorldPos + normal * kShadowBias;
    float rawShadow = traceSoftShadow(rayOrigin, toLight, kShadowTMax, noiseSeed);
    // A different derived seed so AO's samples aren't identical to shadow's.
    float aoSeed = fract(noiseSeed * 2.718281828 + 0.31415926);
    float rawAO = traceAO(rayOrigin, normal, aoSeed);

    // Temporal accumulation: blend this frame's noisy few-sample estimates
    // with history reprojected from last frame, so both terms converge
    // toward a stable, much-higher-effective-sample-count result over a
    // few frames instead of showing raw per-frame noise.
    float currentViewDist = length(frame.cameraPos.xyz - fragWorldPos);
    vec4 prevClip = frame.prevViewProj * vec4(fragWorldPos, 1.0);
    float shadowFactor = rawShadow;
    float aoFactor = rawAO;
    float shadowDisagreementHistory = 0.0;
    if (prevClip.w > 0.001) {
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
            float expectedPrevDist = length(frame.prevCameraPos.xyz - fragWorldPos);
            float distDiff = abs(historySample.z - expectedPrevDist);
            float tolerance = max(0.05 * expectedPrevDist, 0.15);
            if (distDiff < tolerance) {
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
                // per frame, a penumbra pixel's rawShadow is quantized (5
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
                bool isTank = pc.isDynamicObject > 0.5;
                float shadowAlpha;
                if (isTank) {
                    shadowAlpha = 1.0;
                } else {
                    float shadowDisagreement = abs(rawShadow - historySample.x);
                    shadowDisagreementHistory = mix(historySample.w, shadowDisagreement, 0.5);
                    shadowAlpha =
                        mix(0.08, 0.9, clamp((shadowDisagreementHistory - 0.15) * 4.0, 0.0, 1.0));
                }
                shadowFactor = mix(historySample.x, rawShadow, shadowAlpha);

                // AO: same invalid-reprojection reasoning as shadow above
                // applies here too, so skip history for the tank's own
                // surface entirely. Elsewhere, fixed fast blend: with a
                // fixed alpha, (1-alpha)^n of the stale value survives after
                // n frames -- at alpha=0.5 that's 12.5% still left after 3
                // frames, enough to read as a trail. alpha=0.75 leaves under
                // 2% after 3 frames, close enough to call fully resolved.
                float aoAlpha = isTank ? 1.0 : 0.75;
                aoFactor = mix(historySample.y, rawAO, aoAlpha);
            }
        }
    }
    outShadowHistory = vec4(shadowFactor, aoFactor, currentViewDist, shadowDisagreementHistory);

    // Diffuse-only bump: applied after the shadow/AO rays (which stay on the
    // true geometric normal -- perturbing their origin bias or hemisphere
    // basis with a fake micro-bump would just add noise, not detail) but
    // before the diffuse term, which is exactly where a flat-lit decal look
    // comes from.
    vec3 litNormal = normal;
    if (pc.heightBlend > 0.5) {
        litNormal = normalize(normal - vec3(terrainBump.x, 0.0, terrainBump.y));
    } else if (pc.bumpStrength > 0.001) {
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
        vec2 trackBump = vec2(heightU - heightCenter, heightV - heightCenter) * pc.bumpStrength;
        litNormal = normalize(normal - tangent * trackBump.x - bitangent * trackBump.y);
    }
    float diffuse = max(dot(litNormal, toLight), 0.0) * shadowFactor;
    // Ambient/fill term, darkened by AO at contact points (where the tank's
    // tracks, box bases, and tree trunks meet the ground) so those read as
    // grounded rather than floating; faces in shadow still read as dim
    // rather than pure black.
    float lighting = 0.2 * aoFactor + 0.8 * diffuse;

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
    if (pc.isDynamicObject > 0.5) {
        specularStrength = pc.specularStrength * mix(0.5, 1.5, tankSpecularGrain(fragUV));
    }

    // Everything below is gated by specularStrength (0 for terrain/other
    // matte objects), so only opted-in draws (the tank) get these. Real
    // metals have low diffuse reflectance, so the base color is darkened a
    // little here rather than left at full brightness before the reflective
    // terms are layered on -- otherwise those terms just wash the color out
    // toward white instead of reading as a highlight on top of it.
    vec3 base = albedo * lighting * mix(1.0, 0.75, specularStrength) + albedo * dynamicLight;

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
    bool isWater = pc.waveStrength > 0.001;
    if (isWater) {
        float t = frame.cameraPos.w;
        float wave1 = sin(fragWorldPos.x * 1.3 + t * 0.035) * cos(fragWorldPos.z * 1.7 - t * 0.025);
        float wave2 = sin(fragWorldPos.x * 3.1 - t * 0.065 + 1.7) * cos(fragWorldPos.z * 2.3 + t * 0.045);
        vec2 bump = vec2(wave1, wave2) * pc.waveStrength;
        shadingNormal = normalize(normal + vec3(bump.x, 0.0, bump.y));
    }

    // Blinn-Phong specular -- a lower exponent than a glossy/chrome look
    // would use gives a broader, softer highlight, reading as duller,
    // brushed metal rather than polished plastic. Water gets a much
    // tighter, brighter exponent instead -- a real sun-glint on water is a
    // small, sharp highlight, not a broad sheen.
    vec3 halfDir = normalize(toLight + viewDir);
    float specAngle = max(dot(shadingNormal, halfDir), 0.0);
    float specExponent = isWater ? 150.0 : 20.0;
    float specular = pow(specAngle, specExponent) * specularStrength * 0.6 * shadowFactor;

    // Fresnel/rim term: surfaces brighten at grazing view angles, a cheap
    // but very characteristic cue for metal. Kept subtle and tinted toward
    // neutral gray rather than white so it doesn't bleach the paint color.
    // Gated by aoFactor (unlike specular above, which already has
    // shadowFactor) -- grazing angles cluster inside concave nooks (the
    // tank turret's own hatch cavity is the clearest example) exactly where
    // AO is darkest, so without this an occluded crevice still gets a
    // full-strength rim glow that reads as a lit patch floating in shadow.
    // Harmless back when specularStrength was a small flat constant; became
    // visible once the per-pixel specular map (see isDynamicObject above)
    // started pushing it well above that baseline.
    float fresnel =
        pow(1.0 - max(dot(shadingNormal, viewDir), 0.0), 3.0) * specularStrength * 0.18 * aoFactor;

    // Environment reflection. Base case is the analytic sky+cloud function
    // above sampled along the reflection vector -- cheap (no ray/texture),
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
    vec3 reflectDir = reflect(-viewDir, shadingNormal);
    vec3 envColor = skyColor(reflectDir);

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
    float effectiveReflectivity = pc.reflectivity;
    // Recovers the depth fraction WaterGenerator.cpp baked into fragColor
    // (mix(shallowColor, deepColor, depthT), see buildMesh) by projecting
    // back onto that known line -- avoids needing a dedicated depth vertex
    // attribute just for this. Must track WaterGenerator.cpp's palette.
    const vec3 kWaterShallowColor = vec3(0.09, 0.16, 0.14);
    const vec3 kWaterDeepColor = vec3(0.01, 0.025, 0.045);
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
        effectiveReflectivity = mix(pc.reflectivity * 0.25, 0.45, waterFresnel);
        // Fresnel alone floors alpha low for a straight-down view regardless
        // of depth, which reads as "always see the bottom" -- fine for a
        // shallow puddle, wrong for a deep lake. depthAlphaFloor raises that
        // floor with depth so deep water stays substantially opaque even
        // overhead; Fresnel can still push it higher at grazing angles on
        // top of that. Deep end lowered from 0.95 -- fully opaque dark water
        // combined with any reflection blend read as a milky/hazy film
        // rather than dark, clear, and just slightly reflective.
        float depthAlphaFloor = mix(0.18, 0.7, waterDepthT);
        finalAlpha = max(depthAlphaFloor, mix(pc.opacity * 0.4, 0.6, waterFresnel));
    }

    if (effectiveReflectivity > 0.01) {
        vec3 reflectionHit;
        vec3 reflOrigin = fragWorldPos + normal * kShadowBias;
        if (traceReflection(reflOrigin, reflectDir, kShadowTMax, reflectionHit)) {
            envColor = reflectionHit;
        }
    }

    // Extra absorption beyond the deepColor tint itself: water reads as
    // genuinely darker than dry lit ground even at its shallowest (light
    // scattered/absorbed within any water column, however thin), getting
    // properly close to black at the deepest points of a basin -- starting
    // this mix at 1.0 (no darkening at all in shallow water) was what left
    // most of a shallow pond looking barely different from, and about as
    // bright as, the grass around it.
    vec3 baseContribution = isWater ? base * mix(0.45, 0.07, waterDepthT) : base;
    // A true blend rather than adding the reflection on top of the full
    // base color -- the previous `base + env*reflectivity` double-counts
    // brightness (at reflectivity 0.4 you'd get 100% of base AND 40% of a
    // bright sky color, reading as a washed-out pale sheet rather than
    // "mostly transparent, tinted by depth, plus a reflection"). Negligible
    // difference for the tank's own tiny reflectivity (0.06).
    vec3 result = mix(baseContribution, envColor, effectiveReflectivity) + vec3(specular) + fresnel * vec3(0.6);

    // Fogged toward the sky color along the actual camera->fragment
    // direction (not the reflection vector envColor uses above) so it reads
    // as haze sitting between the viewer and the surface, not a reflection.
    // currentViewDist is already computed above for the temporal-history
    // disocclusion check.
    vec3 viewToFragDir = normalize(fragWorldPos - frame.cameraPos.xyz);
    vec3 fogColor = skyGradient(viewToFragDir);
    float fogDist = max(currentViewDist - kFogStartDistance, 0.0);
    float fogFactor = 1.0 - exp(-fogDist * kFogDensity);
    result = mix(result, fogColor, fogFactor);

    outColor = vec4(acesFilmicTonemap(result * kExposure), finalAlpha);
}
