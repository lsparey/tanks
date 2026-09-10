#version 460

// Basic temporal anti-aliasing resolve. Reprojects last frame's blended
// result using the resolved velocity buffer, clamps it into a neighborhood
// color box built from this frame's own HDR color (rejects bad
// reprojection without a separate depth/distance test), blends it with the
// current frame, then sharpens the result to counteract the blur that
// blend introduces. Deliberately simple -- no variance clipping, no
// motion-blurred neighborhoods, no Catmull-Rom history resampling -- see
// PLAN.md's "Linear HDR and temporal image stability" for why.

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D currentColor;
layout(set = 0, binding = 1) uniform sampler2D velocityBuffer;
layout(set = 0, binding = 2) uniform sampler2D historyColor;

layout(push_constant) uniform PushConstants {
    // 0 on the first couple of frames, before both ping-pong history slots
    // hold a real blended result -- see Application's taaHistoryPrimed_.
    float historyValid;
    // F11 toggle, for direct before/after comparison without restarting.
    float taaEnabled;
} pc;

// Fixed weight favoring history for stability once accepted; the
// neighborhood clamp below is what keeps this from ghosting rather than an
// adaptive/disocclusion-aware blend factor.
const float kHistoryWeight = 0.85;
// Neighborhood min/max clamping alone only rejects wrong colors
// (ghosting) -- it does nothing to stop the blend from softening detail,
// since a blurred value can still fall inside the local min/max range of a
// smoothly-varying region. Left uncompensated this reads as a genuinely
// blurry image (confirmed directly: a same-frame TAA-on/off comparison
// showed clearly softened camo/edges, not just a subtle characteristic).
// This is a cheap unsharp mask against the same 4 neighbor taps already
// sampled for the clamp box below -- no new texture reads.
const float kSharpenAmount = 0.15;

void main() {
    vec3 current = texture(currentColor, fragUV).rgb;

    if (pc.taaEnabled < 0.5 || pc.historyValid < 0.5) {
        outColor = vec4(current, 1.0);
        return;
    }

    vec2 velocity = texture(velocityBuffer, fragUV).rg;
    vec2 prevUV = fragUV - velocity;
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        outColor = vec4(current, 1.0);
        return;
    }

    vec3 history = texture(historyColor, prevUV).rgb;

    // 4-tap (not full 3x3) neighborhood min/max box, built from the
    // current frame's own color -- a cheaper stand-in for a full box
    // filter that still catches the common case (a reprojected sample
    // landing on a genuinely different surface/color than what's actually
    // here now).
    vec2 texelSize = 1.0 / vec2(textureSize(currentColor, 0));
    vec3 up = texture(currentColor, fragUV + vec2(0.0, texelSize.y)).rgb;
    vec3 down = texture(currentColor, fragUV - vec2(0.0, texelSize.y)).rgb;
    vec3 left = texture(currentColor, fragUV - vec2(texelSize.x, 0.0)).rgb;
    vec3 right = texture(currentColor, fragUV + vec2(texelSize.x, 0.0)).rgb;
    vec3 neighborMin = min(current, min(min(up, down), min(left, right)));
    vec3 neighborMax = max(current, max(max(up, down), max(left, right)));
    history = clamp(history, neighborMin, neighborMax);

    vec3 blended = mix(current, history, kHistoryWeight);
    vec3 neighborAvg = (up + down + left + right) * 0.25;
    vec3 sharpened = max(blended + (current - neighborAvg) * kSharpenAmount, vec3(0.0));
    outColor = vec4(sharpened, 1.0);
}
