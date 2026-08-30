#include "TrackTextureGenerator.h"

#include <cmath>

#include <glm/glm.hpp>

namespace {

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
    float totalAmplitude = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * smoothNoise(x * frequency, y * frequency);
        totalAmplitude += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / totalAmplitude;
}

}  // namespace

std::vector<uint8_t> TrackTextureGenerator::generate(uint32_t size) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    const glm::vec3 midBrown(0.20f, 0.13f, 0.08f);
    const glm::vec3 darkBrown(0.11f, 0.07f, 0.04f);

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            // Normalized [-1, 1] coordinates, centered on the texture. u
            // runs across the track's width, v along the direction of
            // travel (the mesh stretches this quad via width/length scale,
            // see TrackMark::worldMatrix).
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;

            // The ALPHA itself (not just the color) traces out two separate
            // rails with a gap of undisturbed ground between them -- a
            // solid band across the whole width reads as a road/railway bed,
            // not two tank tracks. Each rail is a soft-edged strip centered
            // at u = +-railCenter; fading at the front/back (v) edges lets
            // consecutive overlapping stamps merge into a continuous strip
            // along the direction of travel.
            // Rails sit near the outer edges of the quad now that the quad's
            // own width is set to the tank's actual hull width (see
            // Application::updateTrackMarks) -- the rails should line up
            // with the outer edges of the tank, not be inset toward center.
            float edgeV = glm::smoothstep(0.85f, 1.0f, std::abs(v));
            float railCenter = 0.78f;
            float railHalfWidth = 0.20f;
            float railDist = std::abs(std::abs(u) - railCenter);
            float railShape = 1.0f - glm::smoothstep(railHalfWidth * 0.7f, railHalfWidth, railDist);
            float shapeAlpha = railShape * (1.0f - edgeV);

            // Periodic ridge segments along the direction of travel within
            // each rail, so it reads as tread links rather than one smooth
            // stripe.
            float grit = fbm(static_cast<float>(x) * 0.12f, static_cast<float>(y) * 0.12f, 3);
            float ridge = glm::step(0.5f, glm::fract(v * 6.0f + 0.5f));
            float darkness = glm::clamp(0.55f + 0.35f * ridge + grit * 0.15f, 0.0f, 1.0f);

            glm::vec3 color = glm::mix(midBrown, darkBrown, darkness);

            size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = static_cast<uint8_t>(glm::clamp(shapeAlpha, 0.0f, 1.0f) * 255.0f);
        }
    }
    return pixels;
}
