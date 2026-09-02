#include "GrassTextureGenerator.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace {

// Wraps a lattice coordinate into [0, period) before hashing, so lattice
// point `period` hashes identically to lattice point 0 -- this is what
// makes the noise genuinely periodic rather than just "coincidentally
// similar" at the seam. period must be a whole number of lattice cells.
int wrapLattice(int coord, int period) {
    int wrapped = coord % period;
    return wrapped < 0 ? wrapped + period : wrapped;
}

float hash(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

// wrapPeriod is the noise's period *in this function's own input space*
// (i.e. smoothNoise(x + wrapPeriod, y + wrapPeriod, wrapPeriod) ==
// smoothNoise(x, y, wrapPeriod) for all x, y) -- see fbm for how callers
// pick it so the texture as a whole tiles seamlessly under GL_REPEAT.
float smoothNoise(float x, float y, int wrapPeriod) {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    float tx = x - static_cast<float>(x0);
    float ty = y - static_cast<float>(y0);
    float sx = tx * tx * (3.0f - 2.0f * tx);
    float sy = ty * ty * (3.0f - 2.0f * ty);

    int wx0 = wrapLattice(x0, wrapPeriod);
    int wx1 = wrapLattice(x0 + 1, wrapPeriod);
    int wy0 = wrapLattice(y0, wrapPeriod);
    int wy1 = wrapLattice(y0 + 1, wrapPeriod);

    float n00 = hash(wx0, wy0);
    float n10 = hash(wx1, wy0);
    float n01 = hash(wx0, wy1);
    float n11 = hash(wx1, wy1);

    float nx0 = n00 + sx * (n10 - n00);
    float nx1 = n01 + sx * (n11 - n01);
    return nx0 + sy * (nx1 - nx0);
}

// basePeriod is the desired tiling period at octave 0, in this call's own
// (possibly pre-scaled) input space -- e.g. for fbm(x*0.05, y*0.05, 4,
// size*0.05), each octave's lattice wraps at size*0.05*2^k, which is
// exactly the point where the *caller's* x has advanced by `size`. That's
// what makes the whole texture -- sampled from (0,0) to (size,size) under
// GL_REPEAT -- loop with no visible seam at the wrap edge, independent of
// whatever constant offset a caller adds to decorrelate two fbm calls
// (periodicity holds for any fixed offset).
float fbm(float x, float y, int octaves, float basePeriod) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float totalAmplitude = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        int wrapPeriod = std::max(1, static_cast<int>(std::round(basePeriod * frequency)));
        sum += amplitude * smoothNoise(x * frequency, y * frequency, wrapPeriod);
        totalAmplitude += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / totalAmplitude;
}

}  // namespace

std::vector<uint8_t> GrassTextureGenerator::generate(uint32_t size, uint32_t variant) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    // A darker, more verdant green than real grass's usual desaturated/olive
    // look -- G kept clearly dominant over R (rather than nearly equal) all
    // the way up to the lightest tone, so it reads as lush green rather than
    // trending yellow-brown at the bright end. Four variants (rather than
    // two) for more visual variety when Application picks which pair to
    // patch-blend together each run -- see Terrain's painting in basic.frag.
    glm::vec3 darkGreen(0.04f, 0.11f, 0.03f);
    glm::vec3 midGreen(0.08f, 0.20f, 0.06f);
    glm::vec3 lightGreen(0.14f, 0.28f, 0.10f);
    switch (variant % 4) {
        case 1:  // drier, more yellow-brown (sun-bleached/trampled)
            darkGreen = glm::vec3(0.07f, 0.11f, 0.03f);
            midGreen = glm::vec3(0.14f, 0.18f, 0.06f);
            lightGreen = glm::vec3(0.22f, 0.24f, 0.09f);
            break;
        case 2:  // deep, cool pine/forest green
            darkGreen = glm::vec3(0.03f, 0.09f, 0.04f);
            midGreen = glm::vec3(0.06f, 0.16f, 0.08f);
            lightGreen = glm::vec3(0.10f, 0.22f, 0.13f);
            break;
        case 3:  // pale, dry straw -- lighter and more yellow than variant 1
            darkGreen = glm::vec3(0.10f, 0.13f, 0.05f);
            midGreen = glm::vec3(0.19f, 0.21f, 0.08f);
            lightGreen = glm::vec3(0.30f, 0.29f, 0.11f);
            break;
        default:
            break;
    }
    // Shifts the noise pattern itself so variant textures don't just look
    // like the same patches recolored -- periodicity (see fbm's comment)
    // holds for any fixed offset, so this doesn't reintroduce a seam.
    float seedX = static_cast<float>(variant) * 173.7f;
    float seedY = static_cast<float>(variant) * 289.3f;

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float patches = fbm(static_cast<float>(x) * 0.05f + seedX, static_cast<float>(y) * 0.05f + seedY,
                                 4, static_cast<float>(size) * 0.05f);
            float speckle = fbm(static_cast<float>(x) * 0.4f + 91.7f + seedX,
                                 static_cast<float>(y) * 0.4f + 13.2f + seedY, 3,
                                 static_cast<float>(size) * 0.4f);
            float t = glm::clamp(patches * 0.65f + speckle * 0.35f, 0.0f, 1.0f);

            glm::vec3 color = t < 0.5f ? glm::mix(darkGreen, midGreen, t * 2.0f)
                                        : glm::mix(midGreen, lightGreen, (t - 0.5f) * 2.0f);

            // Fine per-texel-ish grain, applied as a brightness modulation
            // on the final color -- multiplying the finished color directly
            // (rather than blending another layer into t above, where it'd
            // get smoothstep-interpolated color stops applied on top and
            // washed out) is what actually reads as grain.
            float grain = fbm(static_cast<float>(x) * 0.3f + 47.3f + seedX,
                               static_cast<float>(y) * 0.3f + 205.9f + seedY, 2,
                               static_cast<float>(size) * 0.3f);
            color *= 0.78f + 0.44f * grain;

            // Sharp single-texel flecks, using the raw hash lattice value
            // directly rather than smoothNoise's interpolated version --
            // every layer above (however high its frequency) is
            // smootherstep-interpolated between lattice points by
            // construction, so it's soft no matter how far the frequency is
            // pushed. This is the one genuinely sharp, non-interpolated
            // signal in the whole texture -- without it the result stays a
            // soft blur at any zoom level, which is what actually read as
            // "blurry" up close in-game despite the smooth layers above
            // technically carrying real spatial frequency content.
            float fleck = hash(static_cast<int>(x) * 3 + static_cast<int>(variant) * 7919,
                                static_cast<int>(y) * 5 + static_cast<int>(variant) * 104729);
            if (fleck > 0.9f) {
                color *= 0.55f;
            } else if (fleck < 0.035f) {
                color *= 1.4f;
            }

            size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}
