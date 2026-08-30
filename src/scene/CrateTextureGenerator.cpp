#include "CrateTextureGenerator.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace {

// Non-tileable value noise -- fine here since this texture is mapped
// exactly once per cube face (UV spans [0,1] with no repeat), unlike the
// other generators.
float hash(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

float smoothNoise(float x, float y) {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    float tx = x - static_cast<float>(x0);
    float ty = y - static_cast<float>(y0);
    float sx = tx * tx * (3.0f - 2.0f * tx);
    float sy = ty * ty * (3.0f - 2.0f * ty);
    float n00 = hash(x0, y0);
    float n10 = hash(x0 + 1, y0);
    float n01 = hash(x0, y0 + 1);
    float n11 = hash(x0 + 1, y0 + 1);
    float nx0 = n00 + sx * (n10 - n00);
    float nx1 = n01 + sx * (n11 - n01);
    return nx0 + sy * (nx1 - nx0);
}

float fbm(float x, float y, int octaves) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float total = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * smoothNoise(x * frequency, y * frequency);
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / total;
}

}  // namespace

std::vector<uint8_t> CrateTextureGenerator::generate(uint32_t size) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    const glm::vec3 lightWood(0.62f, 0.46f, 0.28f);
    const glm::vec3 darkWood(0.42f, 0.30f, 0.17f);
    const glm::vec3 seamColor(0.16f, 0.10f, 0.05f);

    // Planks are stacked vertically (along v); grain runs horizontally
    // (along u) within each plank -- low noise frequency along u (long,
    // slow streaks), higher along v (fine cross-grain variation).
    constexpr int kPlankCount = 4;
    constexpr float kSeamHalfWidth = 0.035f;
    constexpr float kBorderWidth = 0.05f;

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);

            float grain = fbm(u * 3.0f, v * 24.0f, 3);
            glm::vec3 color = glm::mix(darkWood, lightWood, grain);

            // Darken near each plank boundary (including the two edges).
            float plankV = v * static_cast<float>(kPlankCount);
            float distToSeam = std::abs(plankV - std::round(plankV));
            float seamDarken = 1.0f - glm::smoothstep(0.0f, kSeamHalfWidth, distToSeam);
            color = glm::mix(color, seamColor, seamDarken);

            // Subtle darkened border around the whole face, like a crate's
            // corner framing.
            float edgeDist = std::min({u, 1.0f - u, v, 1.0f - v});
            float borderDarken = 1.0f - glm::smoothstep(0.0f, kBorderWidth, edgeDist);
            color = glm::mix(color, seamColor, borderDarken * 0.6f);

            size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}
