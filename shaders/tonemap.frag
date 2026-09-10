#version 460

// Single exposure/tonemap/display-encoding stage. Everything upstream
// (shaders/basic.frag) now writes raw linear HDR color into a ResolveTarget;
// this is the one place that maps it down to the swapchain's sRGB output.

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrColor;
// UV-space (current minus previous) motion-vector buffer -- see basic.frag.
// Only consumed by the debug visualization below today; a future TAA blend
// pass will read it for real.
layout(set = 0, binding = 1) uniform sampler2D velocityBuffer;

layout(push_constant) uniform PushConstants {
    float showVelocityDebug;
} pc;

// The swapchain attachment's format is sRGB (see Swapchain::imageFormat_),
// so the driver auto-encodes whatever linear color this shader writes --
// but it does so straight onto an 8-bit target, meaning anything above 1.0
// (bright specular highlights, sun-glint reflections) simply clips to flat
// white with a hard edge. Compressing through a filmic curve first gives
// those highlights a smooth rolloff instead, and pulls the whole image's
// contrast a little closer to how a camera/eye actually responds rather
// than the linear-clip default.
const float kExposure = 1.0;

// Narkowicz 2015 ACES filmic fit -- a widely-used cheap approximation of the
// full ACES tonemap curve, accurate enough for this purpose without needing
// the real curve's 3D LUT. Moved here from basic.frag now that tonemapping
// happens once, after MSAA resolve, instead of once per material branch
// before it.
vec3 acesFilmicTonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Real per-frame UV-space deltas are small (a fraction of a percent even for
// a fast camera pan or the tank's own recoil kick), so this needs a large
// multiplier to read as anything but flat grey in an 8-bit screenshot.
// Tuned to show ordinary camera motion/recoil/wind sway clearly without
// saturating solid; a much faster pan will still clip to a flat tint.
const float kVelocityDebugScale = 150.0;

// Contrast-adaptive sharpening (AMD CAS-style), applied to the tonemapped
// result. The TAA blend upstream keeps its own small fixed unsharp mask,
// but its history resampling still leaves surfaces like the tank's camo
// noticeably soft. CAS scales its kernel weight by inverse local contrast
// -- soft, low-contrast regions get the most sharpening while
// already-contrasty edges get little, which is what avoids the halos a
// fixed-strength unsharp mask produces at this strength. Runs on
// display-referred (post-tonemap) values, the standard placement: in
// linear HDR a bright highlight's huge numeric range would dominate the
// contrast estimate.
const float kSharpness = 0.4;

vec3 tonemapAt(vec2 uv) {
    return acesFilmicTonemap(texture(hdrColor, uv).rgb * kExposure);
}

void main() {
    if (pc.showVelocityDebug > 0.5) {
        // Zero motion reads as flat grey; amplified x/y tint shows
        // direction/magnitude. Verification-only -- see TonemapPass's
        // comment and PLAN.md's "Linear HDR and temporal image stability".
        vec2 velocity = texture(velocityBuffer, fragUV).rg;
        outColor = vec4(clamp(vec3(0.5) + vec3(velocity * kVelocityDebugScale, 0.0), 0.0, 1.0), 1.0);
        return;
    }
    vec2 texel = 1.0 / vec2(textureSize(hdrColor, 0));
    vec3 center = tonemapAt(fragUV);
    vec3 up = tonemapAt(fragUV + vec2(0.0, texel.y));
    vec3 down = tonemapAt(fragUV - vec2(0.0, texel.y));
    vec3 left = tonemapAt(fragUV - vec2(texel.x, 0.0));
    vec3 right = tonemapAt(fragUV + vec2(texel.x, 0.0));

    // Per-channel CAS weight: distance of the local min/max window from
    // 0/1 measures how much headroom there is to steepen this neighborhood
    // without clipping; sqrt biases mid-contrast regions toward fuller
    // sharpening. w is the (negative) cross-neighbor weight of the usual
    // normalized 5-tap sharpen kernel.
    vec3 mn = min(center, min(min(up, down), min(left, right)));
    vec3 mx = max(center, max(max(up, down), max(left, right)));
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0));
    vec3 w = amp * mix(-0.125, -0.2, kSharpness);
    vec3 sharpened = (center + (up + down + left + right) * w) / (1.0 + 4.0 * w);

    outColor = vec4(clamp(sharpened, 0.0, 1.0), texture(hdrColor, fragUV).a);
}
