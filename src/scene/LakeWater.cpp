#include "LakeWater.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace LakeWater {
namespace {
struct Point { glm::vec2 xz; double depth; };
struct Polygon { std::array<Point, 4> points; int size = 0; };

// Strict wet half-space. Zero-depth crossings are emitted once; a triangle
// lying entirely on the lake plane creates neither water nor degenerate faces.
Polygon clip(const std::array<uint32_t, 3>& indices, const std::array<Point, 3>& p) {
    Polygon out;
    for (int k = 0; k < 3; ++k) {
        int next = (k + 1) % 3;
        if (p[k].depth > 0) out.points[out.size++] = p[k];
        if ((p[k].depth > 0) == (p[next].depth > 0)) continue;
        // Canonical edge direction gives neighbouring triangles identical
        // floating-point shoreline vertices, regardless of their winding.
        int a = indices[k] < indices[next] ? k : next;
        int b = a == k ? next : k;
        double t = p[a].depth / (p[a].depth - p[b].depth);
        out.points[out.size++] = {glm::vec2(glm::dvec2(p[a].xz) +
            (glm::dvec2(p[b].xz) - glm::dvec2(p[a].xz)) * t), 0};
    }
    return out;
}
double triangleArea(const Point& a, const Point& b, const Point& c) {
    glm::dvec2 ab = glm::dvec2(b.xz) - glm::dvec2(a.xz), ac = glm::dvec2(c.xz) - glm::dvec2(a.xz);
    return std::abs(ab.x * ac.y - ab.y * ac.x) * .5;
}
double areaAt(const MacroTerrain::Fields& f, uint32_t i) {
    int n = f.heightmap.resolution, x = int(i % n), z = int(i / n);
    return double(f.spacing) * f.spacing * (x == 0 || x == n - 1 ? .5 : 1) *
        (z == 0 || z == n - 1 ? .5 : 1);
}
void finiteQuery(float x, float z) {
    if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("non-finite lake query");
}
}

std::optional<Sample> Surface::sampleAt(float x, float z) const {
    finiteQuery(x, z);
    float half = ground_.heightmap().worldSize * .5f;
    if (x < -half || x > half || z < -half || z > half) return std::nullopt;
    auto triangle = ground_.sampleAt(x, z).triangle;
    int32_t lake = triangleLakes_[triangle];
    if (lake < 0) return std::nullopt;
    float height = levels_[lake], depth = height - ground_.heightAt(x, z);
    if (depth <= 0) return std::nullopt;
    return Sample{uint32_t(lake), height, depth, glm::vec2(0)};
}

std::optional<float> Surface::shorelineDistanceAt(float x, float z) const {
    finiteQuery(x, z);
    auto distance = shoreline_.distanceAt(x, z);
    if (!distance) return std::nullopt;
    return sampleAt(x, z) ? -*distance : *distance;
}

size_t Surface::payloadBytes() const {
    return ground_.heightmap().heights.capacity() * sizeof(float) +
        ground_.shadingNormals().capacity() * sizeof(glm::vec3) + triangleLakes_.capacity() * sizeof(int32_t) +
        levels_.capacity() * sizeof(float) + mesh_.vertices.capacity() * sizeof(Vertex) +
        mesh_.indices.capacity() * sizeof(uint32_t) + shoreline_.payloadBytes();
}
size_t Result::payloadBytes() const {
    return surface.payloadBytes() + lakes.capacity() * sizeof(Lake) + discharge.capacity() * sizeof(double);
}

Result build(const MacroTerrain::Fields& f, const TerrainDrainage::Result& d, const Settings& settings) {
    auto start = std::chrono::steady_clock::now();
    const int n = f.heightmap.resolution, m = f.playableResolution, apron = f.apronCells;
    if (n < 2 || n > 4097 || m < 2 || m > n || apron < 0 || apron > n / 2 || m + 2 * apron != n ||
        !std::isfinite(f.spacing) || f.spacing <= 0 || !std::isfinite(f.playableWorldSize) || f.playableWorldSize <= 0 ||
        !std::isfinite(f.heightmap.worldSize) || f.heightmap.worldSize <= 0 ||
        std::abs(double(f.playableWorldSize) - double(f.spacing) * (m - 1)) > 1e-6 * f.playableWorldSize ||
        std::abs(double(f.heightmap.worldSize) - double(f.spacing) * (n - 1)) > 1e-6 * f.heightmap.worldSize)
        throw std::invalid_argument("invalid lake domain or crop");
    const size_t count = size_t(n) * n;
    if (f.heightmap.heights.size() != count || f.openFaces.size() != count || d.basin.size() != count ||
        d.downstream.size() != count || d.order.size() != count || d.spillElevation.size() != count ||
        !std::isfinite(d.generatedRunoff) || d.generatedRunoff < 0 || !std::isfinite(d.domainArea) || d.domainArea <= 0)
        throw std::invalid_argument("lake drainage dimensions or supply disagree");
    if (!std::isfinite(settings.evaporation) || settings.evaporation < 0 || settings.evaporation > 1 ||
        !std::isfinite(settings.seepage) || settings.seepage < 0 || settings.seepage > 1)
        throw std::invalid_argument("invalid lake loss settings");
    double localRate = d.generatedRunoff / d.domainArea;
    if (!std::isfinite(localRate) || localRate > 1 + 1e-9)
        throw std::invalid_argument("lake supply exceeds drainage rainfall limits");
    std::vector<uint32_t> rank(count, uint32_t(count));
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t i = d.order[k];
        if (i >= count || rank[i] != count) throw std::invalid_argument("invalid lake drainage order");
        rank[i] = k;
    }
    std::vector<int32_t> first(d.basins.size(), -1);
    double domainArea = 0;
    for (uint32_t i : d.order) {
        float h = f.heightmap.heights[i];
        int32_t j = d.downstream[i], b = d.basin[i];
        if (!std::isfinite(h) || !std::isfinite(d.spillElevation[i]) || d.spillElevation[i] < h ||
            double(d.spillElevation[i]) - h > std::numeric_limits<float>::max() ||
            b < -1 || (b >= 0 && size_t(b) >= d.basins.size()) ||
            (b >= 0) != (h < d.spillElevation[i]) || j < -1 ||
            (j >= 0 && (size_t(j) >= count || rank[j] >= rank[i] || d.spillElevation[j] > d.spillElevation[i])) ||
            (j == -1) != (f.openFaces[i] != 0))
            throw std::invalid_argument("invalid or stale lake drainage");
        if (j >= 0) {
            int dx = int(i % n) - j % n, dz = int(i / n) - j / n;
            if (!((std::abs(dx) + std::abs(dz) == 1) || (dx == dz && std::abs(dx) == 1)))
                throw std::invalid_argument("lake route is not a mesh edge");
        }
        if (b >= 0) {
            if (d.basins[b].spillElevation != d.spillElevation[i])
                throw std::invalid_argument("lake basin spill level mismatch");
            if (first[b] < 0) first[b] = int32_t(i);
        }
        domainArea += areaAt(f, i);
    }
    if (std::abs(domainArea - d.domainArea) > 1e-9 * domainArea)
        throw std::invalid_argument("lake drainage area mismatch");
    for (size_t b = 0; b < first.size(); ++b) {
        if (first[b] < 0 || d.basins[b].spillFrom != first[b] ||
            d.basins[b].spillTo != d.downstream[first[b]] || d.downstream[first[b]] < 0)
            throw std::invalid_argument("invalid canonical lake spill edge");
    }

    Result r{Surface(f.crop())};
    std::vector<Shore> shores;
    r.lakes.resize(d.basins.size());
    r.discharge.resize(count);
    r.surface.levels_.resize(d.basins.size());
    r.surface.triangleLakes_.resize(size_t(m - 1) * (m - 1) * 2, -1);
    for (size_t b = 0; b < r.lakes.size(); ++b) {
        auto& lake = r.lakes[b];
        lake.level = d.basins[b].spillElevation;
        lake.spillFrom = first[b]; lake.spillTo = d.downstream[first[b]];
        r.surface.levels_[b] = lake.level;
    }
    auto position = [&](uint32_t i) {
        // Anchor to the playable surface's exact float coordinates. The apron
        // extends that grid rather than introducing a second crop rounding rule.
        return glm::vec2((float(int(i % n) - apron) / (m - 1) - .5f) * f.playableWorldSize,
                         (float(int(i / n) - apron) / (m - 1) - .5f) * f.playableWorldSize);
    };
    auto triangles = [&](auto&& visit) {
        for (int z = 0; z < n - 1; ++z) {
            for (int x = 0; x < n - 1; ++x) {
                auto quad = TerrainSurface::quadIndices(n, x, z);
                for (int t = 0; t < 2; ++t) {
                    std::array<uint32_t, 3> indices{quad[t * 3], quad[t * 3 + 1], quad[t * 3 + 2]};
                    int32_t b = -1;
                    for (uint32_t i : indices) {
                        if (d.basin[i] < 0) continue;
                        if (b >= 0 && b != d.basin[i]) throw std::invalid_argument("incompatible lake basins share a triangle");
                        b = d.basin[i];
                    }
                    if (b < 0) continue;
                    std::array<Point, 3> points;
                    for (int k = 0; k < 3; ++k)
                        points[k] = {position(indices[k]), double(r.lakes[b].level) - f.heightmap.heights[indices[k]]};
                    visit(b, x, z, t, clip(indices, points));
                }
            }
        }
    };
    // Full-domain surface area and storage use the same clipped geometry that
    // will be meshed. Marginal triangles count, including the submerged apron.
    triangles([&](int32_t b, int, int, int, const Polygon& polygon) {
        for (int k = 1; k + 1 < polygon.size; ++k) {
            const auto &a = polygon.points[0], &v = polygon.points[k], &w = polygon.points[k + 1];
            double area = triangleArea(a, v, w);
            r.lakes[b].area += area;
            r.lakes[b].volume += area * (a.depth + v.depth + w.depth) / 3;
        }
    });
    for (uint32_t i = 0; i < count; ++i) {
        r.discharge[i] = localRate * areaAt(f, i);
        r.generatedRunoff += r.discharge[i];
    }
    // Sampled hypsometry for the partial-lake equilibrium solve. Cell heights
    // and dual-cell areas are a bounded sampling of each basin rather than the
    // exact triangle integral used for the full-lake loss capacity.
    std::vector<std::vector<std::pair<float, double>>> hypsometry(r.lakes.size());
    for (uint32_t i = 0; i < count; ++i)
        if (d.basin[i] >= 0) hypsometry[d.basin[i]].push_back({f.heightmap.heights[i], areaAt(f, i)});
    for (auto& cells : hypsometry)
        std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    // Basin members collect inflow instead of forwarding it along the raw
    // flood tree. At the lowest-rank member every inflow is known; excess is
    // released to a strictly earlier vertex, preserving a single acyclic pass.
    for (auto it = d.order.rbegin(); it != d.order.rend(); ++it) {
        uint32_t i = *it;
        int32_t b = d.basin[i], j = d.downstream[i];
        if (b >= 0) {
            auto& lake = r.lakes[b];
            lake.inflow += r.discharge[i];
            r.discharge[i] = 0;
            if (i != uint32_t(lake.spillFrom)) continue;
            double rate = settings.evaporation + settings.seepage;
            double lossCapacity = lake.area * rate;
            lake.loss = std::min(lake.inflow, lossCapacity);
            lake.outflow = lake.inflow - lake.loss;
            lake.present = lake.area > 0 && lake.inflow > 0 && lake.inflow >= lossCapacity;
            if (!lake.present && lake.inflow > 0 && lake.area > 0) {
                // Equilibrium level: rise while the strictly submerged sampled
                // area still loses less than the inflow supplies. The measure
                // mismatch fallback stands at the highest sampled basin cell.
                const auto& cells = hypsometry[b];
                double cumulative = 0;
                float level = cells.back().first;
                for (const auto& [height, area] : cells) {
                    cumulative += area;
                    if (cumulative * rate > lake.inflow) { level = height; break; }
                }
                if (level > d.basins[b].minimumGround) {
                    lake.present = lake.partial = true;
                    lake.level = level;
                    lake.area = lake.volume = 0; // re-integrated at the standing level
                    r.surface.levels_[b] = level;
                }
            }
            r.basinLoss += lake.loss;
            r.discharge[i] = lake.outflow;
        }
        if (j >= 0) r.discharge[j] += r.discharge[i];
        else r.exportedRunoff += r.discharge[i];
    }
    r.runoffResidual = r.exportedRunoff + r.basinLoss - r.generatedRunoff;

    triangles([&](int32_t b, int x, int z, int t, const Polygon& polygon) {
        if (!r.lakes[b].present || polygon.size < 3) return;
        if (r.lakes[b].partial) {
            for (int k = 1; k + 1 < polygon.size; ++k) {
                const auto &a = polygon.points[0], &v = polygon.points[k], &w = polygon.points[k + 1];
                double area = triangleArea(a, v, w);
                r.lakes[b].area += area;
                r.lakes[b].volume += area * (a.depth + v.depth + w.depth) / 3;
            }
        }
        for (int k = 0; k < polygon.size; ++k) {
            const auto& a = polygon.points[k]; const auto& v = polygon.points[(k + 1) % polygon.size];
            if (a.depth == 0 && v.depth == 0 && a.xz != v.xz) shores.push_back({a.xz, v.xz});
        }
        if (x < apron || z < apron || x >= apron + m - 1 || z >= apron + m - 1) return;
        auto& mesh = r.surface.mesh_;
        r.surface.triangleLakes_[2 * (size_t(z - apron) * (m - 1) + x - apron) + t] = b;
        // Emit only positive-area triangles; corner hits and float-collapsed
        // slivers cannot create degenerate raster/BLAS faces.
        for (int k = 1; k + 1 < polygon.size; ++k) {
            if (triangleArea(polygon.points[0], polygon.points[k], polygon.points[k + 1]) <= 0) continue;
            for (int p : {0, k, k + 1}) {
                const auto& point = polygon.points[p];
                mesh.indices.push_back(uint32_t(mesh.vertices.size()));
                mesh.vertices.push_back({{point.xz.x, r.lakes[b].level, point.xz.y}, float(point.depth)});
            }
        }
    });
    r.surface.shoreline_ = ShorelineIndex(std::move(shores));
    r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return r;
}

} // namespace LakeWater
