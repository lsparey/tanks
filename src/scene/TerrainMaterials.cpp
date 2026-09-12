#include "TerrainMaterials.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace TerrainMaterials {
namespace {
float smoothstepAt(float low, float high, float value) {
    float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
float bilinear(const std::vector<float>& values, int resolution, float u, float v) {
    u = std::clamp(u, 0.0f, float(resolution - 1));
    v = std::clamp(v, 0.0f, float(resolution - 1));
    int x = std::min(int(u), resolution - 2), z = std::min(int(v), resolution - 2);
    float fx = u - x, fz = v - z;
    size_t i = size_t(z) * resolution + x;
    float a = std::lerp(values[i], values[i + 1], fx);
    float b = std::lerp(values[i + resolution], values[i + resolution + 1], fx);
    return std::lerp(a, b, fz);
}
float coarseAt(const std::vector<double>& field, int coarse, double scale, int x, int z) {
    double u = x * scale, v = z * scale;
    int cx = std::min(int(u), coarse - 2), cz = std::min(int(v), coarse - 2);
    double fx = u - cx, fz = v - cz;
    size_t i = size_t(cz) * coarse + cx;
    double a = std::lerp(field[i], field[i + 1], fx);
    double b = std::lerp(field[i + coarse], field[i + coarse + 1], fx);
    return float(std::lerp(a, b, fz));
}
float sampleField(const std::vector<float>& values, int resolution, float worldSize, float x, float z) {
    if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("non-finite material query");
    float u = (x / worldSize + .5f) * (resolution - 1);
    float v = (z / worldSize + .5f) * (resolution - 1);
    return bilinear(values, resolution, u, v);
}
}

void validate(const Settings& s) {
    for (float value : {s.soilRockThreshold, s.soilGrassThreshold, s.slopeRockLow, s.slopeRockHigh,
                        s.erosionScarLow, s.erosionScarHigh,
                        s.depositionLow, s.depositionHigh, s.exposureLow, s.exposureHigh,
                        s.throughflowLow, s.throughflowHigh, s.bankMoistureDistance,
                        s.stormMoisture, s.gullyMoisture})
        if (!std::isfinite(value) || value < 0 || value > 1e6f)
            throw std::invalid_argument("invalid material classification reference");
    if (s.soilRockThreshold >= s.soilGrassThreshold || s.slopeRockLow >= s.slopeRockHigh ||
        s.erosionScarLow >= s.erosionScarHigh ||
        s.depositionLow >= s.depositionHigh || s.exposureLow >= s.exposureHigh ||
        s.throughflowLow >= s.throughflowHigh || s.bankMoistureDistance <= 0 ||
        s.stormMoisture > 1 || s.gullyMoisture > 1)
        throw std::invalid_argument("material classification references are not ordered");
}

float Fields::rockAt(float x, float z) const { return sampleField(rock, resolution, worldSize, x, z); }
float Fields::moistureAt(float x, float z) const { return sampleField(moisture, resolution, worldSize, x, z); }
float Fields::sedimentAt(float x, float z) const { return sampleField(sediment, resolution, worldSize, x, z); }
size_t Fields::payloadBytes() const {
    return (rock.capacity() + moisture.capacity() + sediment.capacity()) * sizeof(float);
}

Fields build(const MacroTerrain::Fields& f, const HydraulicErosion::Result& erosion,
             const TerrainDrainage::Result& d, const LakeWater::Result& lakes,
             const TerrainWater::Result& water, const Settings& s) {
    auto started = std::chrono::steady_clock::now();
    validate(s);
    int n = f.heightmap.resolution, m = f.playableResolution, apron = f.apronCells;
    size_t count = size_t(n) * n;
    if (n < 2 || m < 2 || m > n || apron < 0 || m + 2 * apron != n ||
        !std::isfinite(f.spacing) || f.spacing <= 0 ||
        f.heightmap.heights.size() != count || f.soil.size() != count ||
        d.basin.size() != count || lakes.lakes.size() != d.basins.size() ||
        water.streamLevels.size() != count || water.connected.size() != count)
        throw std::invalid_argument("material field dimensions disagree");
    size_t coarseCount = erosion.erosion.size();
    int coarse = int(std::lround(std::sqrt(double(coarseCount))));
    if (size_t(coarse) * coarse != coarseCount || coarse < 2 || (n - 1) % (coarse - 1) != 0 ||
        erosion.deposition.size() != coarseCount || erosion.waterExposure.size() != coarseCount ||
        erosion.throughflow.size() != coarseCount || !erosion.finalized)
        throw std::invalid_argument("erosion diagnostics do not cover the material domain");
    double coarseScale = double(coarse - 1) / (n - 1);

    // Chamfer distance to the FINAL wet surface over the full domain, so bank
    // moisture cannot invent a dry rim at the playable crop edge. Standing
    // lakes count independently of stream connectivity.
    std::vector<float> distance(count, std::numeric_limits<float>::infinity());
    for (uint32_t i = 0; i < count; ++i) {
        if (water.streamLevels[i] <= f.heightmap.heights[i]) continue;
        int32_t b = d.basin[i];
        if (water.connected[i] || (b >= 0 && lakes.lakes[b].present &&
                                   lakes.lakes[b].level > f.heightmap.heights[i]))
            distance[i] = 0;
    }
    const float straight = f.spacing, diagonal = f.spacing * std::sqrt(2.0f);
    auto relax = [&](uint32_t i, int dx, int dz, float cost) {
        int x = int(i % n) + dx, z = int(i / n) + dz;
        if (x < 0 || x >= n || z < 0 || z >= n) return;
        distance[i] = std::min(distance[i], distance[size_t(z) * n + x] + cost);
    };
    for (uint32_t i = 0; i < count; ++i) {
        relax(i, -1, 0, straight); relax(i, 0, -1, straight);
        relax(i, -1, -1, diagonal); relax(i, 1, -1, diagonal);
    }
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t i = uint32_t(count - 1 - k);
        relax(i, 1, 0, straight); relax(i, 0, 1, straight);
        relax(i, 1, 1, diagonal); relax(i, -1, 1, diagonal);
    }

    Fields r;
    r.resolution = m;
    r.worldSize = f.playableWorldSize;
    r.rock.resize(size_t(m) * m);
    r.moisture.resize(size_t(m) * m);
    r.sediment.resize(size_t(m) * m);
    for (int z = 0; z < m; ++z) {
        for (int x = 0; x < m; ++x) {
            size_t out = size_t(z) * m + x;
            int fx = x + apron, fz = z + apron;
            size_t i = size_t(fz) * n + fx;
            auto height = [&](int hx, int hz) {
                return f.heightmap.heights[size_t(std::clamp(hz, 0, n - 1)) * n + std::clamp(hx, 0, n - 1)];
            };
            float gx = (height(fx + 1, fz) - height(fx - 1, fz)) / (2 * f.spacing);
            float gz = (height(fx, fz + 1) - height(fx, fz - 1)) / (2 * f.spacing);
            float slope = std::sqrt(gx * gx + gz * gz);
            float soil = f.soil[i];
            float scar = smoothstepAt(s.erosionScarLow, s.erosionScarHigh,
                                      coarseAt(erosion.erosion, coarse, coarseScale, fx, fz));
            float thin = 1 - smoothstepAt(s.soilRockThreshold, s.soilGrassThreshold, soil);
            float steep = smoothstepAt(s.slopeRockLow, s.slopeRockHigh, slope);
            r.rock[out] = std::clamp(thin * std::max(steep, .8f * scar), 0.0f, 1.0f);
            float bank = std::max(0.0f, 1 - distance[i] / s.bankMoistureDistance);
            float storm = smoothstepAt(s.exposureLow, s.exposureHigh,
                                       coarseAt(erosion.waterExposure, coarse, coarseScale, fx, fz));
            float gully = smoothstepAt(s.throughflowLow, s.throughflowHigh,
                                       coarseAt(erosion.throughflow, coarse, coarseScale, fx, fz));
            r.moisture[out] = std::clamp(std::max({bank, s.stormMoisture * storm, s.gullyMoisture * gully}),
                                         0.0f, 1.0f);
            r.sediment[out] = smoothstepAt(s.depositionLow, s.depositionHigh,
                                           coarseAt(erosion.deposition, coarse, coarseScale, fx, fz));
        }
    }
    r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return r;
}

} // namespace TerrainMaterials
