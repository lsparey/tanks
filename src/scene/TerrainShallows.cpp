#include "TerrainShallows.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace TerrainShallows {
namespace {
Result fill(MacroTerrain::Fields& f, const TerrainDrainage::Result& d, float maximumStandingDepth,
            const std::vector<uint8_t>& removed) {
    auto start = std::chrono::steady_clock::now();
    int n = f.heightmap.resolution;
    size_t count = size_t(n) * n;
    if (n < 2 || n > 4097 || !std::isfinite(f.spacing) || f.spacing <= 0 ||
        !std::isfinite(maximumStandingDepth) || maximumStandingDepth <= 0 ||
        f.heightmap.heights.size() != count || f.soil.size() != count ||
        f.bedrock.size() != count || d.spillElevation.size() != count)
        throw std::invalid_argument("invalid shallow bed settings or dimensions");
    auto heights = f.heightmap.heights, soil = f.soil;
    Result result;
    for (size_t i = 0; i < count; ++i) {
        float h = heights[i], spill = d.spillElevation[i];
        if (!std::isfinite(h) || !std::isfinite(spill) || spill < h ||
            !std::isfinite(soil[i]) || soil[i] < 0 || !std::isfinite(f.bedrock[i]) ||
            h != f.bedrock[i] + soil[i])
            throw std::invalid_argument("invalid or stale shallow bed fields");
        bool remove = !removed.empty() && d.basin[i] >= 0 && removed[d.basin[i]];
        double target = remove ? double(spill) : double(spill) - maximumStandingDepth;
        if (h >= target) continue;
        // Round imported soil upward so the final height respects the bound.
        float targetHeight = float(target);
        if (double(targetHeight) < target)
            targetHeight = std::nextafter(targetHeight, std::numeric_limits<float>::infinity());
        double thickness = double(targetHeight) - f.bedrock[i];
        soil[i] = float(thickness);
        if (double(soil[i]) < thickness)
            soil[i] = std::nextafter(soil[i], std::numeric_limits<float>::infinity());
        heights[i] = f.bedrock[i] + soil[i];
        if (heights[i] < target) {
            soil[i] = std::nextafter(soil[i], std::numeric_limits<float>::infinity());
            heights[i] = f.bedrock[i] + soil[i];
        }
        // Removed basins round up to the spill so no sub-ulp puddle remains.
        if (heights[i] < target || (!remove && heights[i] > spill))
            throw std::invalid_argument("shallow bed depth is below terrain precision");
        int x = int(i % n), z = int(i / n);
        double area = double(f.spacing) * f.spacing * (x == 0 || x == n - 1 ? .5 : 1) *
                      (z == 0 || z == n - 1 ? .5 : 1);
        result.addedSoil += (double(soil[i]) - f.soil[i]) * area;
    }
    f.heightmap.heights = std::move(heights);
    f.soil = std::move(soil);
    result.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
}
Result apply(MacroTerrain::Fields& f, const TerrainDrainage::Result& d, float maximumStandingDepth) {
    return fill(f, d, maximumStandingDepth, {});
}
Result removeSmallLakes(MacroTerrain::Fields& f, const TerrainDrainage::Result& d,
                        const LakeWater::Result& lakes, double minimumArea, float minimumRadius) {
    auto start = std::chrono::steady_clock::now();
    int n = f.heightmap.resolution, m = f.playableResolution, apron = f.apronCells;
    if (!std::isfinite(minimumArea) || minimumArea < 0 || !std::isfinite(minimumRadius) || minimumRadius < 0 ||
        n < 2 || n > 4097 || m < 2 || apron < 0 || m + 2 * int64_t(apron) != n ||
        !std::isfinite(f.playableWorldSize) || f.playableWorldSize <= 0 ||
        f.heightmap.heights.size() != size_t(n) * n ||
        d.basin.size() != f.heightmap.heights.size() || lakes.lakes.size() != d.basins.size())
        throw std::invalid_argument("invalid small lake filter settings or dimensions");
    std::vector<uint8_t> removed(lakes.lakes.size());
    for (size_t b = 0; b < removed.size(); ++b) removed[b] = lakes.lakes[b].present;
    for (size_t i = 0; i < d.basin.size(); ++i) {
        int b = d.basin[i];
        if (b < 0) continue;
        if (size_t(b) >= removed.size()) throw std::invalid_argument("invalid small lake basin index");
        const auto& lake = lakes.lakes[b];
        if (!removed[b] || lake.area < minimumArea || f.heightmap.heights[i] >= lake.level) continue;
        float x = (float(int(i % n) - apron) / (m - 1) - .5f) * f.playableWorldSize;
        float z = (float(int(i / n) - apron) / (m - 1) - .5f) * f.playableWorldSize;
        auto distance = lakes.surface.shorelineDistanceAt(x, z);
        if (!distance || std::abs(*distance) >= minimumRadius) removed[b] = 0;
    }
    uint32_t count = uint32_t(std::count(removed.begin(), removed.end(), uint8_t(1)));
    // Infinity would fail validation; this finite depth leaves every retained
    // basin untouched within the terrain module's supported height range.
    auto result = count ? fill(f, d, 2e6f, removed) : Result{};
    result.removedLakes = count;
    result.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
}
