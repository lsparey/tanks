#include "TerrainRefinement.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace TerrainRefinement {
namespace {
// Linear boundary continuation preserves planes at the outer edges. Interior
// cubic samples use the four-point Catmull-Rom midpoint stencil; clamp the
// result to the containing coarse cell to avoid creating new height extrema.
double sample(const std::vector<float>& values, int n, int x, int z) {
    auto row = [&](int zz) {
        if (x < 0) return 2.0 * values[size_t(zz) * n] - values[size_t(zz) * n + 1];
        if (x >= n) return 2.0 * values[size_t(zz) * n + n - 1] - values[size_t(zz) * n + n - 2];
        return double(values[size_t(zz) * n + x]);
    };
    if (z < 0) return 2 * row(0) - row(1);
    if (z >= n) return 2 * row(n - 1) - row(n - 2);
    return row(z);
}
float interpolate(const std::vector<float>& values, int n, int x, int z) {
    int cx = x / 2, cz = z / 2;
    constexpr double w[4]{-.0625, .5625, .5625, -.0625};
    double value = 0;
    for (int j = 0; j < (z % 2 ? 4 : 1); ++j)
        for (int i = 0; i < (x % 2 ? 4 : 1); ++i)
            value += (x % 2 ? w[i] : 1) * (z % 2 ? w[j] : 1) *
                     sample(values, n, cx + (x % 2 ? i - 1 : 0), cz + (z % 2 ? j - 1 : 0));
    double low = values[size_t(cz) * n + cx], high = low;
    for (int j = 0; j <= z % 2; ++j) for (int i = 0; i <= x % 2; ++i) {
        double v = values[size_t(cz + j) * n + cx + i];
        low = std::min(low, v); high = std::max(high, v);
    }
    return float(std::clamp(value, low, high));
}
double volume(const std::vector<float>& values, int n, double spacing) {
    double sum = 0;
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x)
        sum += double(values[size_t(z) * n + x]) * spacing * spacing *
               ((x == 0 || x == n - 1) ? .5 : 1) * ((z == 0 || z == n - 1) ? .5 : 1);
    return sum;
}
Result applyOnce(MacroTerrain::Fields& source) {
    auto start = std::chrono::steady_clock::now();
    int n = source.heightmap.resolution;
    if (n < 2 || n > 2049 || source.playableResolution < 2 || source.apronCells < 0 ||
        source.apronCells > n / 2 || source.playableResolution != n - 2 * source.apronCells ||
        !std::isfinite(source.spacing) || source.spacing < .0001f ||
        !std::isfinite(source.heightmap.worldSize) || source.heightmap.worldSize <= 0 ||
        !std::isfinite(source.playableWorldSize) || source.playableWorldSize <= 0 ||
        std::abs(double(source.spacing) * (n - 1) - source.heightmap.worldSize) > 1e-6 * source.heightmap.worldSize ||
        std::abs(double(source.spacing) * (source.playableResolution - 1) - source.playableWorldSize) > 1e-6 * source.playableWorldSize)
        throw std::invalid_argument("invalid refinement domain");
    size_t count = size_t(n) * n;
    if (source.heightmap.heights.size() != count || source.bedrock.size() != count ||
        source.soil.size() != count || source.erodibility.size() != count || source.openFaces.size() != count)
        throw std::invalid_argument("invalid refinement field sizes");
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
        size_t i = size_t(z) * n + x;
        int boundary = (x == 0 ? 1 : 0) | (x == n - 1 ? 2 : 0) | (z == 0 ? 4 : 0) | (z == n - 1 ? 8 : 0);
        if (!std::isfinite(source.heightmap.heights[i]) || !std::isfinite(source.bedrock[i]) ||
            !std::isfinite(source.soil[i]) || source.soil[i] < 0 ||
            source.heightmap.heights[i] != source.bedrock[i] + source.soil[i] ||
            !std::isfinite(source.erodibility[i]) || source.erodibility[i] < 0 || source.erodibility[i] > 1 ||
            (source.openFaces[i] & ~boundary))
            throw std::invalid_argument("invalid refinement material or outlet");
    }
    int m = 2 * n - 1;
    MacroTerrain::Fields fine;
    fine.heightmap = {m, source.heightmap.worldSize, std::vector<float>(size_t(m) * m)};
    fine.playableResolution = 2 * source.playableResolution - 1;
    fine.playableWorldSize = source.playableWorldSize;
    fine.apronCells = 2 * source.apronCells;
    fine.spacing = source.spacing * .5f;
    fine.bedrock.resize(size_t(m) * m); fine.soil.resize(size_t(m) * m);
    fine.erodibility.resize(size_t(m) * m); fine.openFaces.resize(size_t(m) * m);
    for (int z = 0; z < m; ++z) for (int x = 0; x < m; ++x) {
        size_t i = size_t(z) * m + x, parent = size_t(z / 2) * n + x / 2;
        if (x % 2 == 0 && z % 2 == 0) {
            fine.bedrock[i] = source.bedrock[parent]; fine.soil[i] = source.soil[parent];
            fine.erodibility[i] = source.erodibility[parent]; fine.openFaces[i] = source.openFaces[parent];
        } else {
            float height = interpolate(source.heightmap.heights, n, x, z);
            fine.soil[i] = interpolate(source.soil, n, x, z);
            fine.bedrock[i] = height - fine.soil[i];
            fine.erodibility[i] = interpolate(source.erodibility, n, x, z);
            if (x == 0 || x == m - 1)
                fine.openFaces[i] = source.openFaces[parent] & source.openFaces[parent + n];
            else if (z == 0 || z == m - 1)
                fine.openFaces[i] = source.openFaces[parent] & source.openFaces[parent + 1];
        }
        fine.heightmap.heights[i] = fine.bedrock[i] + fine.soil[i];
        if (!std::isfinite(fine.heightmap.heights[i]) || !std::isfinite(fine.bedrock[i]))
            throw std::invalid_argument("unrepresentable refined column");
    }
    Result result;
    result.sourceResolution = n; result.targetResolution = m;
    result.soilVolumeDelta = volume(fine.soil, m, fine.spacing) - volume(source.soil, n, source.spacing);
    result.bedrockVolumeDelta = volume(fine.bedrock, m, fine.spacing) - volume(source.bedrock, n, source.spacing);
    source = std::move(fine);
    result.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
}
int parsePasses(std::string_view value) {
    if (value == "off") return 0;
    if (value == "on" || value == "2x") return 1;
    if (value == "4x") return 2;
    throw std::invalid_argument("--terrain-refinement requires off, on, 2x or 4x");
}
Result apply(MacroTerrain::Fields& source, int passes) {
    if (passes < 1 || passes > 2 || source.heightmap.resolution > 1 + 4096 / (1 << passes))
        throw std::invalid_argument("invalid refinement passes or domain exceeds 4097 samples");
    if (passes == 1) return applyOnce(source);
    auto start = std::chrono::steady_clock::now();
    // Work on a copy so a failure in either pass leaves the caller's fields intact.
    auto fine = source;
    auto result = applyOnce(fine);
    auto second = applyOnce(fine);
    result.targetResolution = second.targetResolution;
    result.soilVolumeDelta += second.soilVolumeDelta;
    result.bedrockVolumeDelta += second.bedrockVolumeDelta;
    source = std::move(fine);
    result.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
}
