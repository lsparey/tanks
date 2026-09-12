#include "RockOutcrops.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

#include <glm/glm.hpp>

namespace RockOutcrops {
namespace {
double hashAt(uint32_t seed, int x, int z) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(z) * 668265263u + seed * 2654435761u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return double((h ^ (h >> 16)) & 0xFFFFFFu) / double(0xFFFFFF);
}
double valueNoise(uint32_t seed, glm::dvec2 p) {
    int x0 = int(std::floor(p.x)), z0 = int(std::floor(p.y));
    double tx = p.x - x0, tz = p.y - z0;
    double sx = tx * tx * (3 - 2 * tx), sz = tz * tz * (3 - 2 * tz);
    double a = std::lerp(hashAt(seed, x0, z0), hashAt(seed, x0 + 1, z0), sx);
    double b = std::lerp(hashAt(seed, x0, z0 + 1), hashAt(seed, x0 + 1, z0 + 1), sx);
    return std::lerp(a, b, sz);
}
double fbm(uint32_t seed, glm::dvec2 p, int octaves) {
    double sum = 0, amplitude = .5, total = 0;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * valueNoise(seed + uint32_t(i) * 101u, p);
        total += amplitude;
        amplitude *= .5;
        p *= 2.0;
    }
    return sum / total;
}
float smoothstepAt(float low, float high, float value) {
    float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
}

void validate(const Settings& s) {
    for (float value : {s.slopeLow, s.slopeHigh, s.soilCover, s.strataHeight, s.terraceStrength,
                        s.reliefAmplitude, s.maximumChange, s.soilStripping})
        if (!std::isfinite(value) || value < 0 || value > 100)
            throw std::invalid_argument("invalid rock outcrop settings");
    if (s.slopeLow >= s.slopeHigh || s.soilCover <= 0 || s.strataHeight <= 0 ||
        s.maximumChange <= 0 || s.maximumChange > 2 || s.terraceStrength > 1 || s.soilStripping > 1)
        throw std::invalid_argument("rock outcrop settings are not ordered or bounded");
}

Result apply(MacroTerrain::Fields& f, uint32_t seed, const Settings& s) {
    auto started = std::chrono::steady_clock::now();
    validate(s);
    int n = f.heightmap.resolution, m = f.playableResolution, apron = f.apronCells;
    size_t count = size_t(n) * n;
    if (n < 2 || m < 2 || m > n || apron < 0 || m + 2 * apron != n ||
        !std::isfinite(f.spacing) || f.spacing <= 0 ||
        f.heightmap.heights.size() != count || f.bedrock.size() != count || f.soil.size() != count)
        throw std::invalid_argument("invalid rock outcrop domain");

    // Exposure from the PRE-displacement slope so the pass is a single
    // deterministic function of its input, not an iterated relaxation.
    const auto original = f.heightmap.heights;
    auto heightAt = [&](int x, int z) {
        return original[size_t(std::clamp(z, 0, n - 1)) * n + std::clamp(x, 0, n - 1)];
    };
    Result r;
    auto rounded = [](double value) { return float(value); };
    std::vector<float> change(count, 0), strip(count, 0);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            size_t i = size_t(z) * n + x;
            float gx = (heightAt(x + 1, z) - heightAt(x - 1, z)) / (2 * f.spacing);
            float gz = (heightAt(x, z + 1) - heightAt(x, z - 1)) / (2 * f.spacing);
            float slope = std::sqrt(gx * gx + gz * gz);
            float expose = smoothstepAt(s.slopeLow, s.slopeHigh, slope) *
                           (1 - smoothstepAt(0.0f, s.soilCover, f.soil[i]));
            // Break the exposure up so ledges appear in patches, not as a
            // uniform skirt around every steep face.
            glm::dvec2 world((double(x - apron) / (m - 1) - .5) * f.playableWorldSize,
                             (double(z - apron) / (m - 1) - .5) * f.playableWorldSize);
            expose *= smoothstepAt(.35f, .6f, float(fbm(seed ^ 0x9e37u, world * 0.11, 3)));
            // Rare crag bands CREATE scarps on merely moderate slopes instead
            // of only decorating already-steep ground: within a large-scale
            // noise patch, terracing strengthens and its strata grow tall
            // enough to read as a stepped rock face at tank scale.
            float crag = smoothstepAt(.62f, .74f, float(fbm(seed ^ 0x77c3u, world * 0.045, 3))) *
                         smoothstepAt(.22f, .4f, slope);
            expose = std::max(expose, crag);
            if (expose <= 0) continue;

            // Terraced strata: snap toward horizontal ledges, plus a ridged
            // crest/hollow so faces are not perfect stair-steps.
            double h = original[i];
            double strata = std::lerp(double(s.strataHeight), double(s.strataHeight) * 2.4, double(crag));
            double level = h / strata;
            double terrace = (std::floor(level) +
                              smoothstepAt(.25f, .75f, float(level - std::floor(level)))) *
                             strata;
            double ridged = 1 - std::abs(2 * fbm(seed ^ 0x51edu, world * 0.55, 4) - 1);
            double strength = std::min(1.0, double(s.terraceStrength) + .15 * crag);
            double bound = double(s.maximumChange) * (1 + crag);
            double displaced = std::lerp(h, terrace, strength * expose) +
                               (ridged - .5) * s.reliefAmplitude * expose;
            change[i] = float(std::clamp(displaced - h, -bound, bound));
            strip[i] = s.soilStripping * expose;
        }
    }
    // One tent-filter pass rounds the ledges at mesh scale: raw per-vertex
    // terracing at fine grid spacing reads as hard pyramid facets, while
    // heavier smoothing flattens the crags back out. The filter is a convex
    // combination, so every bound above still holds.
    for (int pass = 0; pass < 1; ++pass) {
        auto source = change;
        auto at = [&](int x, int z) {
            return source[size_t(std::clamp(z, 0, n - 1)) * n + std::clamp(x, 0, n - 1)];
        };
        for (int z = 0; z < n; ++z) {
            for (int x = 0; x < n; ++x) {
                float sum = 4 * at(x, z) + 2 * (at(x - 1, z) + at(x + 1, z) + at(x, z - 1) + at(x, z + 1)) +
                            at(x - 1, z - 1) + at(x + 1, z - 1) + at(x - 1, z + 1) + at(x + 1, z + 1);
                change[size_t(z) * n + x] = sum / 16;
            }
        }
    }
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            size_t i = size_t(z) * n + x;
            if (change[i] == 0 && strip[i] <= 0) continue;
            float soil = rounded(f.soil[i] * (1 - strip[i]));
            double area = double(f.spacing) * f.spacing *
                          ((x == 0 || x == n - 1) ? .5 : 1) * ((z == 0 || z == n - 1) ? .5 : 1);
            float bedrock = rounded(double(f.bedrock[i]) + change[i] + (double(f.soil[i]) - soil));
            r.soilVolumeDelta += (double(soil) - f.soil[i]) * area;
            r.bedrockVolumeDelta += (double(bedrock) - f.bedrock[i]) * area;
            f.soil[i] = soil;
            f.bedrock[i] = bedrock;
            f.heightmap.heights[i] = bedrock + soil;
            ++r.changedCells;
        }
    }
    r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return r;
}

} // namespace RockOutcrops
