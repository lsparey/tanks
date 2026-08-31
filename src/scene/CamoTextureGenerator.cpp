#include "CamoTextureGenerator.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace {

// Same hash/smoothNoise/fbm shape as the other tileable texture generators
// (see GrassTextureGenerator, BarkTextureGenerator) -- each keeps its own
// copy rather than sharing one, matching this codebase's convention of
// small, self-contained generator files.
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

std::vector<uint8_t> CamoTextureGenerator::generate(uint32_t size) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    const glm::vec3 darkGreen(0.07f, 0.10f, 0.05f);
    const glm::vec3 brown(0.15f, 0.10f, 0.05f);
    const glm::vec3 tan(0.30f, 0.25f, 0.15f);
    const glm::vec3 black(0.03f, 0.03f, 0.03f);

    float fsize = static_cast<float>(size);

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);

            // Domain-warp the sample point with its own (differently
            // seeded) tileable noise field before evaluating the main
            // blotch layer -- makes the blotch boundaries read as
            // irregular/hand-painted rather than the perfectly smooth
            // contours a single fbm call produces. Both the warp fields and
            // the main layer share the same base period (size * 0.02), so
            // the periodicity that makes each one individually tile also
            // holds for the composition: warping x by a period-`size`
            // function of x still leaves the result period-`size` in x.
            float warpX = fbm(fx * 0.02f + 40.0f, fy * 0.02f + 40.0f, 3, fsize * 0.02f);
            float warpY = fbm(fx * 0.02f + 90.0f, fy * 0.02f + 90.0f, 3, fsize * 0.02f);
            float wx = fx + (warpX - 0.5f) * fsize * 0.15f;
            float wy = fy + (warpY - 0.5f) * fsize * 0.15f;
            float blotch = fbm(wx * 0.02f, wy * 0.02f, 3, fsize * 0.02f);

            // A finer, independent layer breaks the largest blotches into
            // smaller sub-patches, closer to a real multi-tone scheme than
            // one giant blob per color.
            float fine = fbm(fx * 0.07f + 17.0f, fy * 0.07f + 17.0f, 3, fsize * 0.07f);
            float t = glm::clamp(blotch * 0.7f + fine * 0.3f, 0.0f, 1.0f);

            // Hard-edged color bands (a narrow smoothstep transition, not a
            // full gradient) -- reads as distinct painted patches rather
            // than a continuous color blend like this project's other
            // (organic-material) texture generators.
            glm::vec3 color;
            if (t < 0.32f) {
                color = darkGreen;
            } else if (t < 0.36f) {
                color = glm::mix(darkGreen, brown, glm::smoothstep(0.32f, 0.36f, t));
            } else if (t < 0.62f) {
                color = brown;
            } else if (t < 0.66f) {
                color = glm::mix(brown, tan, glm::smoothstep(0.62f, 0.66f, t));
            } else if (t < 0.86f) {
                color = tan;
            } else if (t < 0.90f) {
                color = glm::mix(tan, black, glm::smoothstep(0.86f, 0.90f, t));
            } else {
                color = black;
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
