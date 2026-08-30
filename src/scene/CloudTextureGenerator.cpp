#include "CloudTextureGenerator.h"

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
// pick it so the texture as a whole tiles seamlessly under GL_REPEAT (the
// cloud dome's UV can run well past [0,1] near the horizon, so this
// matters even more here than for the ground textures).
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

std::vector<uint8_t> CloudTextureGenerator::generate(uint32_t size) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    const glm::vec3 brightColor(1.0f, 1.0f, 1.0f);
    const glm::vec3 shadowColor(0.72f, 0.75f, 0.80f);

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            // Large-scale shapes (few octaves, low frequency) for big cloud
            // masses; a finer layer riding on top gives them a bit of
            // cauliflower-like internal texture rather than a flat blob.
            float shape = fbm(static_cast<float>(x) * 0.02f, static_cast<float>(y) * 0.02f, 4,
                               static_cast<float>(size) * 0.02f);
            float detail = fbm(static_cast<float>(x) * 0.08f + 41.3f, static_cast<float>(y) * 0.08f + 7.1f,
                                3, static_cast<float>(size) * 0.08f);
            float density = shape * 0.75f + detail * 0.25f;

            // Mostly-clear sky with occasional soft-edged raised patches --
            // the two smoothstep thresholds control how much of the sky is
            // cloudy and how soft the cloud edges read.
            float alpha = glm::smoothstep(0.52f, 0.70f, density);
            // Slightly darker where density is high (thicker cloud core),
            // brighter at the thin edges -- a cheap stand-in for real
            // self-shadowing/ambient occlusion within the cloud shape.
            glm::vec3 color = glm::mix(brightColor, shadowColor, glm::smoothstep(0.6f, 0.9f, density));

            size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = static_cast<uint8_t>(glm::clamp(alpha, 0.0f, 1.0f) * 255.0f);
        }
    }
    return pixels;
}
