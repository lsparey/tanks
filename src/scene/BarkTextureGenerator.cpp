#include "BarkTextureGenerator.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace {

// Wraps a lattice coordinate into [0, period) before hashing, so lattice
// point `period` hashes identically to lattice point 0 -- makes the noise
// genuinely periodic (needed since this texture tiles both around a
// branch's circumference and along its length via GL_REPEAT).
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

float smoothNoise(float x, float y, int wrapX, int wrapY) {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    float tx = x - static_cast<float>(x0);
    float ty = y - static_cast<float>(y0);
    float sx = tx * tx * (3.0f - 2.0f * tx);
    float sy = ty * ty * (3.0f - 2.0f * ty);

    int wx0 = wrapLattice(x0, wrapX);
    int wx1 = wrapLattice(x0 + 1, wrapX);
    int wy0 = wrapLattice(y0, wrapY);
    int wy1 = wrapLattice(y0 + 1, wrapY);

    float n00 = hash(wx0, wy0);
    float n10 = hash(wx1, wy0);
    float n01 = hash(wx0, wy1);
    float n11 = hash(wx1, wy1);

    float nx0 = n00 + sx * (n10 - n00);
    float nx1 = n01 + sx * (n11 - n01);
    return nx0 + sy * (nx1 - nx0);
}

// basePeriodX/Y are the desired tiling periods at octave 0 -- see
// GrassTextureGenerator.cpp for the full reasoning on why this makes the
// texture as a whole loop seamlessly under GL_REPEAT.
float fbm(float x, float y, int octaves, float basePeriodX, float basePeriodY) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float totalAmplitude = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        int wrapX = std::max(1, static_cast<int>(std::round(basePeriodX * frequency)));
        int wrapY = std::max(1, static_cast<int>(std::round(basePeriodY * frequency)));
        sum += amplitude * smoothNoise(x * frequency, y * frequency, wrapX, wrapY);
        totalAmplitude += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / totalAmplitude;
}

}  // namespace

std::vector<uint8_t> BarkTextureGenerator::generate(uint32_t size, uint32_t variant) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    // Four palettes so Application's small pool of tree variants doesn't
    // all share one identical trunk color.
    glm::vec3 darkBrown(0.10f, 0.07f, 0.04f);
    glm::vec3 midBrown(0.20f, 0.14f, 0.08f);
    glm::vec3 lightBrown(0.32f, 0.23f, 0.14f);
    switch (variant % 4) {
        case 1:  // desaturated grey-brown, ash-like
            darkBrown = glm::vec3(0.09f, 0.08f, 0.07f);
            midBrown = glm::vec3(0.17f, 0.15f, 0.13f);
            lightBrown = glm::vec3(0.27f, 0.24f, 0.21f);
            break;
        case 2:  // reddish, cedar-like
            darkBrown = glm::vec3(0.13f, 0.06f, 0.03f);
            midBrown = glm::vec3(0.26f, 0.12f, 0.06f);
            lightBrown = glm::vec3(0.38f, 0.19f, 0.11f);
            break;
        case 3:  // pale, birch-like
            darkBrown = glm::vec3(0.16f, 0.14f, 0.12f);
            midBrown = glm::vec3(0.30f, 0.27f, 0.23f);
            lightBrown = glm::vec3(0.46f, 0.42f, 0.37f);
            break;
        default:
            break;
    }
    // Shifts the noise pattern itself so variant textures don't just look
    // like the same fissure pattern recolored -- periodicity (see fbm's
    // comment) holds for any fixed offset, so this doesn't reintroduce a
    // seam.
    float seedX = static_cast<float>(variant) * 149.1f;
    float seedY = static_cast<float>(variant) * 263.7f;

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            // Stretched noise (much lower frequency across X, the
            // around-the-branch direction, than along Y) for fissures that
            // run mostly lengthwise along the trunk/branch, the way real
            // bark furrows do.
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);
            float fissures = fbm(fx * 0.12f + seedX, fy * 0.03f + seedY, 4, static_cast<float>(size) * 0.12f,
                                  static_cast<float>(size) * 0.03f);
            float grain = fbm(fx * 0.5f + 33.1f + seedX, fy * 0.5f + 71.9f + seedY, 2,
                               static_cast<float>(size) * 0.5f, static_cast<float>(size) * 0.5f);
            float t = glm::clamp(fissures * 0.75f + grain * 0.25f, 0.0f, 1.0f);

            glm::vec3 color = t < 0.5f ? glm::mix(darkBrown, midBrown, t * 2.0f)
                                        : glm::mix(midBrown, lightBrown, (t - 0.5f) * 2.0f);

            size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}
