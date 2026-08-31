#include "MetalTextureGenerator.h"

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

std::vector<uint8_t> MetalTextureGenerator::generate(uint32_t size) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    const glm::vec3 darkGunmetal(0.05f, 0.05f, 0.055f);
    const glm::vec3 lightGunmetal(0.12f, 0.12f, 0.13f);

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);

            // Stretched noise (much lower frequency across Y than X) for
            // brushing marks that run mostly in one direction, the way real
            // brushed/machined metal catches the light in streaks.
            float brushing = fbm(fx * 0.5f, fy * 0.03f, 3, static_cast<float>(size) * 0.5f,
                                  static_cast<float>(size) * 0.03f);
            float speckle = fbm(fx * 0.2f + 61.0f, fy * 0.2f + 21.0f, 2, static_cast<float>(size) * 0.2f,
                                 static_cast<float>(size) * 0.2f);
            float t = glm::clamp(brushing * 0.6f + speckle * 0.4f, 0.0f, 1.0f);

            glm::vec3 color = glm::mix(darkGunmetal, lightGunmetal, t);

            size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}
