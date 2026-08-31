#include "RockTextureGenerator.h"

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
// (possibly pre-scaled) input space -- see GrassTextureGenerator.cpp's copy
// of this function for the full reasoning.
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

std::vector<uint8_t> RockTextureGenerator::generate(uint32_t size, uint32_t variant) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    // Darker than real gravel's usual mid-grey, and hue-varied rather than
    // just two brightness levels of the same warm-grey tone -- variant 0 is
    // a cool, near-neutral dark slate/stone; variant 1+ is a warmer, more
    // saturated dark earth/dirt tone (R noticeably > G > B), so the two
    // patch-blend into genuinely different-looking ground the way grass's
    // lush-green/dry-yellow-brown variants do, rather than reading as one
    // material lightened -- see Terrain's painting in basic.frag.
    glm::vec3 darkGrey(0.08f, 0.08f, 0.09f);
    glm::vec3 midGrey(0.15f, 0.15f, 0.16f);
    glm::vec3 lightGrey(0.24f, 0.23f, 0.22f);
    if (variant % 2 == 1) {
        darkGrey = glm::vec3(0.10f, 0.06f, 0.03f);
        midGrey = glm::vec3(0.20f, 0.13f, 0.07f);
        lightGrey = glm::vec3(0.32f, 0.21f, 0.11f);
    }
    // Shifts the noise pattern itself so variant textures don't just look
    // like the same patches recolored -- periodicity (see fbm's comment)
    // holds for any fixed offset, so this doesn't reintroduce a seam.
    float seedX = static_cast<float>(variant) * 211.3f;
    float seedY = static_cast<float>(variant) * 337.9f;

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float patches = fbm(static_cast<float>(x) * 0.045f + seedX, static_cast<float>(y) * 0.045f + seedY,
                                 4, static_cast<float>(size) * 0.045f);
            // Higher frequency and more weight than grass's speckle layer --
            // gravel reads as individual pebbles, not a soft blade texture.
            float speckle = fbm(static_cast<float>(x) * 0.6f + 91.7f + seedX,
                                 static_cast<float>(y) * 0.6f + 13.2f + seedY, 3,
                                 static_cast<float>(size) * 0.6f);
            float t = glm::clamp(patches * 0.55f + speckle * 0.45f, 0.0f, 1.0f);

            glm::vec3 color = t < 0.5f ? glm::mix(darkGrey, midGrey, t * 2.0f)
                                        : glm::mix(midGrey, lightGrey, (t - 0.5f) * 2.0f);
            color *= 0.5f;

            size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}
