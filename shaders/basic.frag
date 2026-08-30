#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_ray_tracing_position_fetch : require

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragWorldPos;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 prevViewProj;
    vec4 lightDir;   // direction the light travels, xyz
    vec4 cameraPos;
    vec4 prevCameraPos;
} frame;

layout(set = 1, binding = 0) uniform sampler2D materialTex;
// Only sampled/blended in when pc.heightBlend is nonzero (terrain); every
// other draw binds the same texture as materialTex here and this is simply
// never read.
layout(set = 1, binding = 1) uniform sampler2D materialTexLow;
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
    vec3 skyTint = vec3(0.55, 0.65, 0.78);
    vec3 groundTint = vec3(0.12, 0.11, 0.10);
    vec3 color = mix(groundTint, skyTint, clamp(dir.y * 0.5 + 0.5, 0.0, 1.0));
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

// Orthonormal basis around `n`, used to jitter a ray direction within a
// small cone (soft shadows) or hemisphere (AO) instead of a single fixed
// direction.
void buildBasis(vec3 n, out vec3 t, out vec3 b) {
    vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

const int kShadowSamples = 5;
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
    return sum / float(kShadowSamples);
}

const int kAOSamples = 8;
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

void main() {
    vec4 texSample = texture(materialTex, fragUV);
    vec3 texColor = texSample.rgb;
    if (pc.heightBlend > 0.5) {
        // Terrain: fade to the low-point (rock/gravel) texture in valleys.
        // Thresholds are tuned against the heightmap's actual amplitude
        // (+-3 world units, see HeightmapGenerator) -- below -1.3, fully
        // rock; above -0.4, fully grass; smoothstep between so the seam
        // doesn't read as a hard line.
        vec3 texColorLow = texture(materialTexLow, fragUV).rgb;
        float rockiness = 1.0 - smoothstep(-1.3, -0.4, fragWorldPos.y);
        texColor = mix(texColor, texColorLow, rockiness);
    }
    vec3 albedo = fragColor * texColor;
    // Texture alpha times the per-draw opacity (PushConstants::opacity) --
    // both are 1.0 for every opaque draw in the scene, so this only actually
    // does something for fading ground decals like TrackMark, whose texture
    // has a soft alpha falloff and whose opacity decreases as it ages.
    float finalAlpha = texSample.a * pc.opacity;

    if (pc.unlit > 0.5) {
        outColor = vec4(albedo, finalAlpha);
        outShadowHistory = vec4(1.0, 1.0, 50000.0, 0.0);
        return;
    }

    vec3 normal = normalize(fragNormal);
    vec3 toLight = normalize(-frame.lightDir.xyz);

    // Offset the ray origin along the normal to avoid self-shadowing
    // ("shadow acne") from the surface the ray starts on. Directional light
    // has no real distance limit, so tMax just needs to comfortably exceed
    // the scene's extent (terrain worldSize is 60).
    const float kShadowBias = 0.02;
    const float kShadowTMax = 200.0;
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
                // shadow entirely; specularStrength is a unique tag for tank
                // fragments (0.6, vs 0 for everything else drawn).
                bool isTank = pc.specularStrength > 0.5;
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

    float diffuse = max(dot(normal, toLight), 0.0) * shadowFactor;
    // Ambient/fill term, darkened by AO at contact points (where the tank's
    // tracks, box bases, and tree trunks meet the ground) so those read as
    // grounded rather than floating; faces in shadow still read as dim
    // rather than pure black.
    float lighting = 0.2 * aoFactor + 0.8 * diffuse;

    // Everything below is gated by specularStrength (0 for terrain/other
    // matte objects), so only opted-in draws (the tank) get these. Real
    // metals have low diffuse reflectance, so the base color is darkened a
    // little here rather than left at full brightness before the reflective
    // terms are layered on -- otherwise those terms just wash the color out
    // toward white instead of reading as a highlight on top of it.
    vec3 base = albedo * lighting * mix(1.0, 0.75, pc.specularStrength);

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
    if (pc.waveStrength > 0.001) {
        float t = frame.cameraPos.w;
        float wave1 = sin(fragWorldPos.x * 1.3 + t * 0.035) * cos(fragWorldPos.z * 1.7 - t * 0.025);
        float wave2 = sin(fragWorldPos.x * 3.1 - t * 0.065 + 1.7) * cos(fragWorldPos.z * 2.3 + t * 0.045);
        vec2 bump = vec2(wave1, wave2) * pc.waveStrength;
        shadingNormal = normalize(normal + vec3(bump.x, 0.0, bump.y));
    }

    // Blinn-Phong specular -- a lower exponent than a glossy/chrome look
    // would use gives a broader, softer highlight, reading as duller,
    // brushed metal rather than polished plastic.
    vec3 halfDir = normalize(toLight + viewDir);
    float specAngle = max(dot(shadingNormal, halfDir), 0.0);
    float specular = pow(specAngle, 20.0) * pc.specularStrength * 0.6 * shadowFactor;

    // Fresnel/rim term: surfaces brighten at grazing view angles, a cheap
    // but very characteristic cue for metal. Kept subtle and tinted toward
    // neutral gray rather than white so it doesn't bleach the paint color.
    float fresnel = pow(1.0 - max(dot(shadingNormal, viewDir), 0.0), 3.0) * pc.specularStrength * 0.18;

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
    if (pc.reflectivity > 0.01) {
        vec3 reflectionHit;
        vec3 reflOrigin = fragWorldPos + normal * kShadowBias;
        if (traceReflection(reflOrigin, reflectDir, kShadowTMax, reflectionHit)) {
            envColor = reflectionHit;
        }
    }

    vec3 result = base + vec3(specular) + fresnel * vec3(0.6) + envColor * pc.reflectivity;
    outColor = vec4(result, finalAlpha);
}
