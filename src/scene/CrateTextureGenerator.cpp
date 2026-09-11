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

    const glm::vec3 lightWood(0.66f, 0.50f, 0.30f);
    const glm::vec3 darkWood(0.38f, 0.26f, 0.145f);
    const glm::vec3 seamColor(0.14f, 0.09f, 0.045f);
    const glm::vec3 nailColor(0.28f, 0.27f, 0.26f);

    // Planks are stacked vertically (along v); grain runs horizontally
    // (along u) within each plank. Each plank carries its own board tone and
    // grain phase (real crates are nailed together from whatever boards were
    // on hand, and identical planks were the old texture's biggest tell),
    // plus a chance of a knot. A darker perimeter frame with corner nails
    // reads as the crate's batten framing.
    constexpr int kPlankCount = 4;
    constexpr float kSeamHalfWidth = 0.03f;
    constexpr float kBorderWidth = 0.055f;

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);

            int plank = std::min(static_cast<int>(v * kPlankCount), kPlankCount - 1);
            float vv = v * kPlankCount - static_cast<float>(plank);  // 0..1 within this plank

            // Board-to-board variation: brightness and a slight warm/grey
            // shift per plank.
            float plankTone = 0.84f + hash(plank * 17 + 3, 41) * 0.32f;
            float plankHue = hash(plank * 29 + 7, 91);

            // Grain: long streaks along u, wobbled by a low-frequency warp so
            // the streak lines waver like real figure instead of running
            // ruler-straight, plus a finer streak layer for close-up detail.
            float wobble = fbm(u * 2.0f + static_cast<float>(plank) * 13.7f, vv * 3.0f, 2);
            float grain = fbm(u * 3.5f + static_cast<float>(plank) * 31.9f,
                              (vv + wobble * 0.4f) * 9.0f, 3);
            float streak = fbm(u * 16.0f + static_cast<float>(plank) * 77.1f, vv * 34.0f, 2);
            float g = glm::clamp(grain * 0.68f + streak * 0.32f, 0.0f, 1.0f);
            // Sharpen: pushes the blended noise toward distinct light/dark
            // grain lines rather than a soft brown blur.
            g = glm::smoothstep(0.24f, 0.78f, g);
            glm::vec3 color = glm::mix(darkWood, lightWood, g) * plankTone;
            color = glm::mix(color, color * glm::vec3(1.06f, 0.96f, 0.86f), plankHue * 0.6f);

            // Knots: up to two candidate spots per plank, each present about
            // half the time -- a dark core with faint growth rings, and the
            // surrounding grain darkened slightly like wood around a knot.
            for (int k = 0; k < 2; ++k) {
                if (hash(plank * 67 + k * 131, 9) > 0.5f) continue;
                float ku = 0.14f + hash(plank * 53 + k * 101, 5) * 0.72f;
                float kv = 0.28f + hash(plank * 91 + k * 163, 3) * 0.44f;
                // u compressed so the knot stays roundish in texel space
                // despite the plank being 4x wider than tall.
                float dist =
                    glm::length(glm::vec2((u - ku) * static_cast<float>(kPlankCount), vv - kv)) /
                    0.17f;
                if (dist >= 1.8f) continue;
                float rings = 0.5f + 0.5f * std::sin(dist * 8.0f + ku * 40.0f);
                float knotMask = 1.0f - glm::smoothstep(0.55f, 1.15f, dist);
                glm::vec3 knotColor = glm::mix(darkWood * 0.5f, darkWood * 0.95f, rings);
                color = glm::mix(color, knotColor, knotMask);
                color = glm::mix(color, seamColor, 1.0f - glm::smoothstep(0.0f, 0.3f, dist));
                // Faint halo of compressed grain just outside the knot.
                float halo = glm::smoothstep(1.0f, 1.25f, dist) *
                             (1.0f - glm::smoothstep(1.25f, 1.8f, dist));
                color *= 1.0f - halo * 0.12f;
            }

            // Darken toward each plank boundary, with a thin catch-light just
            // above the seam so the boards read as separate slats with real
            // thickness instead of painted-on lines.
            float plankV = v * static_cast<float>(kPlankCount);
            float distToSeam = std::abs(plankV - std::round(plankV));
            float seamDarken = 1.0f - glm::smoothstep(0.0f, kSeamHalfWidth, distToSeam);
            float bevel = glm::smoothstep(kSeamHalfWidth, kSeamHalfWidth * 2.4f, distToSeam) *
                          (1.0f - glm::smoothstep(kSeamHalfWidth * 2.4f, kSeamHalfWidth * 5.0f,
                                                  distToSeam));
            color = glm::mix(color, seamColor, seamDarken);
            color *= 1.0f + bevel * 0.14f;

            // Perimeter frame: its own slightly darker boards with grain
            // running around the face (v-major), separated from the field
            // planks by a shadow line at the inner frame edge.
            float edgeDist = std::min({u, 1.0f - u, v, 1.0f - v});
            float frameMask = 1.0f - glm::smoothstep(kBorderWidth * 0.9f, kBorderWidth, edgeDist);
            float frameGrain = glm::smoothstep(
                0.2f, 0.8f, fbm(v * 3.2f + 51.7f, u * 24.0f, 3));
            glm::vec3 frameColor = glm::mix(darkWood, lightWood, frameGrain) * 0.72f;
            color = glm::mix(color, frameColor, frameMask * 0.9f);
            float frameLine =
                1.0f - glm::smoothstep(0.004f, 0.014f, std::abs(edgeDist - kBorderWidth));
            color = glm::mix(color, seamColor, frameLine * 0.65f);

            // Nail heads pinning the frame: one near each corner and one at
            // each edge midpoint, a small dark disc with an off-centre
            // highlight so it reads as a domed head catching the light.
            const glm::vec2 nailPositions[8] = {
                {0.028f, 0.028f}, {0.5f, 0.028f}, {0.972f, 0.028f}, {0.028f, 0.5f},
                {0.972f, 0.5f},   {0.028f, 0.972f}, {0.5f, 0.972f}, {0.972f, 0.972f},
            };
            for (const glm::vec2& nail : nailPositions) {
                glm::vec2 d(u - nail.x, v - nail.y);
                float dist = glm::length(d);
                float mask = 1.0f - glm::smoothstep(0.008f, 0.014f, dist);
                if (mask <= 0.0f) continue;
                color = glm::mix(color, nailColor, mask);
                float highlight =
                    1.0f - glm::smoothstep(0.0f, 0.006f, glm::length(d + glm::vec2(0.004f)));
                color += glm::vec3(0.22f) * highlight;
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
