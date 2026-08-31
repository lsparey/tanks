#include "BoundaryTextureGenerator.h"

#include <cmath>

#include <glm/glm.hpp>

namespace {

// Same hash/smoothNoise/fbm shape as the other texture generators (see
// TrackTextureGenerator, CrateTextureGenerator) -- each keeps its own copy
// rather than sharing one, matching the existing convention in this
// codebase of small, self-contained generator files.
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

void writePixel(std::vector<uint8_t>& pixels, uint32_t size, uint32_t x, uint32_t y, glm::vec3 color,
                 float alpha) {
    size_t idx = (static_cast<size_t>(y) * size + x) * 4;
    pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
    pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
    pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
    pixels[idx + 3] = static_cast<uint8_t>(glm::clamp(alpha, 0.0f, 1.0f) * 255.0f);
}

}  // namespace

std::vector<uint8_t> BoundaryTextureGenerator::generateGroundLine(uint32_t size) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    const glm::vec3 darkRed(0.45f, 0.03f, 0.02f);
    const glm::vec3 brightRed(0.85f, 0.10f, 0.06f);

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);

            // Soft-edged strip across the width (u), roughened by a coarser
            // noise mask so the edge reads as hand-painted onto uneven grass
            // rather than a perfectly clean stripe -- the mask both nibbles
            // the outer edge ragged and leaves faint patchy gaps within the
            // body of the line itself.
            float distFromCenter = std::abs(u - 0.5f) * 2.0f;
            float edgeNoise = fbm(u * 5.0f, v * 14.0f, 3);
            float edgeShape = 1.0f - glm::smoothstep(0.45f + edgeNoise * 0.35f, 0.85f + edgeNoise * 0.35f,
                                                       distFromCenter);
            float patchNoise = fbm(u * 9.0f + 50.0f, v * 26.0f + 50.0f, 3);
            float alpha = edgeShape * glm::mix(0.55f, 1.0f, patchNoise);

            // Per-pixel color variation reads as uneven paint coverage/
            // pigment rather than a flat fill.
            float colorNoise = fbm(u * 11.0f + 120.0f, v * 31.0f + 120.0f, 2);
            glm::vec3 color = glm::mix(darkRed, brightRed, colorNoise);

            writePixel(pixels, size, x, y, color, alpha);
        }
    }
    return pixels;
}

std::vector<uint8_t> BoundaryTextureGenerator::generateWall(uint32_t size) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    const glm::vec3 hotRed(1.0f, 0.32f, 0.20f);   // near the ground, closer to white-hot
    const glm::vec3 deepRed(0.85f, 0.04f, 0.03f);  // higher up

    // Overall cap well under 1 -- "mostly transparent" -- with the real
    // fade-to-invisible happening over the upper portion of the texture so
    // the wall mesh's own top edge (tuned to sit above the tallest trees,
    // see BoundaryGenerator::buildWallMesh) reads as a soft vanishing point
    // rather than a hard cutoff.
    constexpr float kBaseAlpha = 0.25f;

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);

            // Narrow smoothstep range plus a squared falloff -- most of the
            // opacity is gone within the bottom third of the wall's height
            // rather than fading gradually most of the way to the top.
            float heightFade = 1.0f - glm::smoothstep(0.0f, 0.35f, v);
            heightFade *= heightFade;
            // Gentle vertical shimmer streaks -- stretched noise (low
            // frequency along v, higher along u) so it reads as flickering
            // energy columns rather than a uniform flat glow.
            float shimmer = fbm(u * 10.0f, v * 2.5f, 3) * 0.5f + 0.5f;
            float alpha = kBaseAlpha * heightFade * glm::mix(0.7f, 1.0f, shimmer);

            glm::vec3 color = glm::mix(hotRed, deepRed, glm::clamp(v * 1.3f, 0.0f, 1.0f));

            writePixel(pixels, size, x, y, color, alpha);
        }
    }
    return pixels;
}
