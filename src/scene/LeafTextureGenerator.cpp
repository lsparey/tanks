#include "LeafTextureGenerator.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace {

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

std::vector<uint8_t> LeafTextureGenerator::generate(uint32_t size, uint32_t variant) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    // Four palettes so Application's small pool of tree variants doesn't all
    // share one identical canopy color.
    glm::vec3 darkGreen(0.06f, 0.14f, 0.04f);
    glm::vec3 midGreen(0.13f, 0.26f, 0.09f);
    glm::vec3 lightGreen(0.22f, 0.38f, 0.14f);
    switch (variant % 4) {
        case 1:  // yellow-green, autumn-leaning
            darkGreen = glm::vec3(0.10f, 0.13f, 0.03f);
            midGreen = glm::vec3(0.22f, 0.24f, 0.06f);
            lightGreen = glm::vec3(0.36f, 0.34f, 0.10f);
            break;
        case 2:  // dark pine/conifer green
            darkGreen = glm::vec3(0.03f, 0.09f, 0.05f);
            midGreen = glm::vec3(0.07f, 0.17f, 0.10f);
            lightGreen = glm::vec3(0.12f, 0.26f, 0.16f);
            break;
        case 3:  // cool blue-green
            darkGreen = glm::vec3(0.05f, 0.13f, 0.11f);
            midGreen = glm::vec3(0.10f, 0.24f, 0.20f);
            lightGreen = glm::vec3(0.17f, 0.35f, 0.29f);
            break;
        default:
            break;
    }
    // Shifts the noise pattern itself so variant textures don't just look
    // like the same clump pattern recolored -- periodicity (see fbm's
    // comment) holds for any fixed offset, so this doesn't reintroduce a
    // seam.
    float seedX = static_cast<float>(variant) * 191.3f;
    float seedY = static_cast<float>(variant) * 307.9f;

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);
            // Higher frequency than grass -- reads as a mass of small
            // leaves/needles rather than blades, since this is applied
            // close-up on canopy clusters.
            float clumps = fbm(fx * 0.15f + seedX, fy * 0.15f + seedY, 3, static_cast<float>(size) * 0.15f);
            float speckle = fbm(fx * 0.9f + 17.3f + seedX, fy * 0.9f + 52.6f + seedY, 2,
                                 static_cast<float>(size) * 0.9f);
            float t = glm::clamp(clumps * 0.55f + speckle * 0.45f, 0.0f, 1.0f);

            glm::vec3 color = t < 0.5f ? glm::mix(darkGreen, midGreen, t * 2.0f)
                                        : glm::mix(midGreen, lightGreen, (t - 0.5f) * 2.0f);

            size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}
