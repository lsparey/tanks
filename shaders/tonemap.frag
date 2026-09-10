#version 460

// Single exposure/tonemap/display-encoding stage. Everything upstream
// (shaders/basic.frag) now writes raw linear HDR color into HdrTarget;
// this is the one place that maps it down to the swapchain's sRGB output.

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrColor;

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

void main() {
    vec4 hdr = texture(hdrColor, fragUV);
    outColor = vec4(acesFilmicTonemap(hdr.rgb * kExposure), hdr.a);
}
