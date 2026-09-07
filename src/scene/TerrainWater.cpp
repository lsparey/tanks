#include "TerrainWater.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace TerrainWater {
namespace {
struct Point { glm::vec2 xz; double ground, stream; };
using Polygon = std::vector<Point>;
enum class Cut { AboveLake, BelowLake, WetStream, WetLake };
double value(const Point& p, Cut cut, double lake) {
    switch (cut) {
        case Cut::AboveLake: return p.stream - lake;
        case Cut::BelowLake: return lake - p.stream;
        case Cut::WetStream: return p.stream - p.ground;
        case Cut::WetLake: return lake - p.ground;
    }
    return 0;
}
Polygon clip(const Polygon& polygon, Cut cut, double lake) {
    Polygon result;
    for (size_t k = 0; k < polygon.size(); ++k) {
        Point a = polygon[k], b = polygon[(k + 1) % polygon.size()];
        double av = value(a, cut, lake), bv = value(b, cut, lake);
        if (av >= 0) result.push_back(a);
        if ((av < 0) == (bv < 0)) continue;
        // Match vertex order on shared terrain edges and on the new stream/
        // lake seam. Partition at equal levels BEFORE either shoreline clip.
        if (a.xz.y > b.xz.y || (a.xz.y == b.xz.y && a.xz.x > b.xz.x)) { std::swap(a, b); std::swap(av, bv); }
        double t = av / (av - bv);
        Point p{glm::vec2(glm::dvec2(a.xz) + t * (glm::dvec2(b.xz) - glm::dvec2(a.xz))),
                std::lerp(a.ground, b.ground, t), std::lerp(a.stream, b.stream, t)};
        if (cut == Cut::AboveLake || cut == Cut::BelowLake) p.stream = lake;
        else p.ground = cut == Cut::WetStream ? p.stream : lake;
        result.push_back(p);
    }
    return result;
}
double area(const Point& a, const Point& b, const Point& c) {
    auto ab = glm::dvec2(b.xz) - glm::dvec2(a.xz), ac = glm::dvec2(c.xz) - glm::dvec2(a.xz);
    return std::abs(ab.x * ac.y - ab.y * ac.x) * .5;
}
void finiteQuery(float x, float z) {
    if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("non-finite combined water query");
}
}

std::optional<Sample> Surface::sampleAt(float x, float z) const {
    finiteQuery(x, z);
    float half = ground_.heightmap().worldSize * .5f;
    if (x < -half || x > half || z < -half || z > half) return std::nullopt;
    auto sample = ground_.sampleAt(x, z);
    const auto& triangle = triangles_[sample.triangle];
    const auto& heights = ground_.heightmap().heights;
    double firstGround = heights[sample.indices[0]];
    double ground = firstGround + (double(heights[sample.indices[1]]) - firstGround) * sample.weights.y +
                                 (double(heights[sample.indices[2]]) - firstGround) * sample.weights.z;
    std::optional<Sample> result;
    if (triangle.lake >= 0 && triangle.lakeLevel > ground)
        result = Sample{Kind::Lake, triangle.lake, triangle.lakeLevel, float(triangle.lakeLevel - ground), {0, 0}};
    if (triangle.stream) {
        double first = streamLevels_[sample.indices[0]];
        double level = first + (double(streamLevels_[sample.indices[1]]) - first) * sample.weights.y +
                               (double(streamLevels_[sample.indices[2]]) - first) * sample.weights.z;
        if (level > ground && (!result || level > result->height))
            result = Sample{Kind::Stream, -1, float(level), float(level - ground), triangle.flow};
    }
    return result;
}
std::optional<float> Surface::shorelineDistanceAt(float x, float z) const {
    finiteQuery(x, z);
    auto distance = shoreline_.distanceAt(x, z);
    if (!distance) return std::nullopt;
    return sampleAt(x, z) ? -*distance : *distance;
}
bool Surface::triangleHasWater(uint32_t index) const {
    const auto& triangle = triangles_.at(index);
    int n = ground_.heightmap().resolution;
    uint32_t quad = index / 2;
    auto indices = TerrainSurface::quadIndices(n, quad % (n - 1), quad / (n - 1));
    for (int k = 0; k < 3; ++k) {
        uint32_t i = indices[(index % 2) * 3 + k];
        float ground = ground_.heightmap().heights[i];
        if ((triangle.lake >= 0 && triangle.lakeLevel > ground) ||
            (triangle.stream && streamLevels_[i] > ground)) return true;
    }
    return false;
}
size_t Surface::payloadBytes() const {
    return ground_.heightmap().heights.capacity() * sizeof(float) + ground_.shadingNormals().capacity() * sizeof(glm::vec3) +
        streamLevels_.capacity() * sizeof(float) + triangles_.capacity() * sizeof(Triangle) +
        mesh_.vertices.capacity() * sizeof(Vertex) + mesh_.indices.capacity() * sizeof(uint32_t) +
        shoreline_.payloadBytes();
}
size_t Result::payloadBytes() const {
    return surface.payloadBytes() + streamLevels.capacity() * sizeof(float) + connected.capacity() * sizeof(uint8_t);
}

Result build(const MacroTerrain::Fields& f, const TerrainDrainage::Result& d,
             const LakeWater::Result& lakes, const StreamNetwork::Result& network) {
    auto started = std::chrono::steady_clock::now();
    int n = f.heightmap.resolution, m = f.playableResolution, apron = f.apronCells;
    if (n < 2 || n > 4097 || m < 2 || m > n || apron < 0 || apron > n / 2 || m + 2 * apron != n ||
        !std::isfinite(f.spacing) || f.spacing <= 0 || !std::isfinite(f.playableWorldSize) || f.playableWorldSize <= 0 ||
        !std::isfinite(f.heightmap.worldSize) || f.heightmap.worldSize <= 0 ||
        std::abs(double(f.spacing) * (m - 1) - f.playableWorldSize) > f.playableWorldSize * 1e-6 ||
        std::abs(double(f.spacing) * (n - 1) - f.heightmap.worldSize) > f.heightmap.worldSize * 1e-6)
        throw std::invalid_argument("invalid combined water domain");
    size_t count = size_t(n) * n;
    if (f.heightmap.heights.size() != count || d.basin.size() != count || d.spillElevation.size() != count ||
        d.downstream.size() != count || d.order.size() != count || lakes.lakes.size() != d.basins.size())
        throw std::invalid_argument("combined water dimensions disagree");
    std::vector<float> coordinates;
    for (int i = 0; i < n; ++i) {
        float p = (float(i - apron) / (m - 1) - .5f) * f.playableWorldSize;
        if (!std::isfinite(p) || (!coordinates.empty() && p <= coordinates.back()))
            throw std::invalid_argument("unrepresentable combined water coordinates");
        coordinates.push_back(p);
    }
    auto position = [&](uint32_t i) { return glm::vec2(coordinates[i % n], coordinates[i / n]); };
    std::vector<uint32_t> drainageRank(count, uint32_t(count));
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t i = d.order[k];
        if (i >= count || drainageRank[i] != count) throw std::invalid_argument("invalid combined water drainage order");
        drainageRank[i] = k;
    }
    for (uint32_t i = 0; i < count; ++i) {
        float h = f.heightmap.heights[i]; int32_t b = d.basin[i];
        int32_t j = d.downstream[i];
        if (!std::isfinite(h) || std::abs(h) > 1e6 || !std::isfinite(d.spillElevation[i]) || d.spillElevation[i] < h ||
            b < -1 || (b >= 0 && size_t(b) >= lakes.lakes.size()) || (b >= 0) != (h < d.spillElevation[i]) ||
            (b >= 0 && lakes.lakes[b].level != d.spillElevation[i]) || j < -1 ||
            (j >= 0 && (size_t(j) >= count || drainageRank[j] >= drainageRank[i] || d.spillElevation[j] > d.spillElevation[i])))
            throw std::invalid_argument("invalid or stale combined water ground/basins");
        if (j >= 0) {
            int dx = int(i % n) - j % n, dz = int(i / n) - j / n;
            if (!((std::abs(dx) + std::abs(dz) == 1) || (dx == dz && std::abs(dx) == 1)))
                throw std::invalid_argument("combined water drainage left terrain mesh edges");
        }
    }
    std::vector<int32_t> nodeAt(count, -1);
    if (network.downstreamOrder.size() != network.nodes.size()) throw std::invalid_argument("invalid combined water network order");
    std::vector<uint32_t> rank(network.nodes.size(), uint32_t(network.nodes.size()));
    for (uint32_t k = 0; k < rank.size(); ++k) {
        uint32_t i = network.downstreamOrder[k];
        if (i >= rank.size() || rank[i] != rank.size()) throw std::invalid_argument("invalid combined water network order");
        rank[i] = k;
    }
    auto level = [](const StreamNetwork::Node& a) { return a.kind == StreamNetwork::Kind::DrySink ? a.ground : a.waterLevel; };
    for (uint32_t i = 0; i < network.nodes.size(); ++i) {
        const auto& a = network.nodes[i];
        int32_t j = a.downstream;
        if (a.cell >= count || a.ground != f.heightmap.heights[a.cell] || a.position != position(a.cell) ||
            !std::isfinite(a.waterLevel) || a.waterLevel < a.ground || std::abs(a.waterLevel) > 1e6 ||
            j < -1 || (j >= 0 && (size_t(j) >= network.nodes.size() || rank[j] >= rank[i] ||
                                  d.downstream[a.cell] != int32_t(network.nodes[j].cell))))
            throw std::invalid_argument("invalid or stale combined water stream profile");
        int32_t b = d.basin[a.cell];
        if ((a.kind == StreamNetwork::Kind::LakeInlet || a.kind == StreamNetwork::Kind::LakeOutlet) &&
            (b < 0 || !lakes.lakes[b].present || a.waterLevel != lakes.lakes[b].level))
            throw std::invalid_argument("combined water stream does not meet its lake level");
        if (a.kind == StreamNetwork::Kind::DrySink && (b < 0 || lakes.lakes[b].present))
            throw std::invalid_argument("combined water dry sink does not match basin supply");
        if (nodeAt[a.cell] >= 0 && level(network.nodes[nodeAt[a.cell]]) != level(a))
            throw std::invalid_argument("conflicting water levels at shared stream vertex");
        nodeAt[a.cell] = int32_t(i);
        if (j < 0) continue;
        const auto& target = network.nodes[j];
        if (target.cell >= count) throw std::invalid_argument("invalid stream destination");
        int dx = int(a.cell % n) - int(target.cell % n), dz = int(a.cell / n) - int(target.cell / n);
        if (!((std::abs(dx) + std::abs(dz) == 1) || (dx == dz && std::abs(dx) == 1)) || level(a) < level(target))
            throw std::invalid_argument("combined water route is not a descending mesh edge");
    }
    Result r{Surface(f.crop())};
    std::vector<LakeWater::Shore> shores;
    r.streamLevels = f.heightmap.heights;
    r.connected.resize(count);
    std::vector<glm::vec2> guideFlow(count, glm::vec2(0)), nodeFlow(network.nodes.size(), glm::vec2(0));
    for (uint32_t i = 0; i < network.nodes.size(); ++i) {
        const auto& a = network.nodes[i];
        if (a.downstream < 0) continue;
        auto direction = glm::vec2(glm::normalize(glm::dvec2(network.nodes[a.downstream].position) - glm::dvec2(a.position)));
        nodeFlow[i] = direction;
        if (network.nodes[a.downstream].downstream < 0) nodeFlow[a.downstream] += direction;
    }
    for (auto& flow : nodeFlow) if (glm::length(flow) > 0) flow = glm::normalize(flow);
    // A bank inherits its FIRST downstream stream or reservoir, not a nearby
    // tributary in another catchment. Project along that stream's outgoing
    // edge to preserve the longitudinal profile. No radius or width cutoff
    // invents a shoreline on terrain that would still be submerged.
    std::vector<int32_t> receiver(count, -1); // >=0 stream node; -2-b lake; -1 dry
    for (uint32_t i : d.order) {
        int32_t basin = d.basin[i];
        if (basin >= 0) {
            if (lakes.lakes[basin].present) receiver[i] = -2 - basin;
        } else if (nodeAt[i] >= 0) receiver[i] = nodeAt[i];
        else if (d.downstream[i] >= 0) receiver[i] = receiver[d.downstream[i]];
        int32_t owner = receiver[i];
        if (owner < -1) r.streamLevels[i] = lakes.lakes[-2 - owner].level;
        if (owner < 0) continue;
        const auto& a = network.nodes[owner];
        r.streamLevels[i] = level(a); guideFlow[i] = nodeFlow[owner];
        if (a.downstream >= 0) {
            const auto& b = network.nodes[a.downstream];
            auto start = glm::dvec2(a.position), edge = glm::dvec2(b.position) - start;
            double t = std::clamp(glm::dot(glm::dvec2(position(i)) - start, edge) / glm::dot(edge, edge), 0.0, 1.0);
            r.streamLevels[i] = float(std::lerp(double(level(a)), double(level(b)), t));
        }
    }
    // Discard isolated wet predictions. A point contact at zero depth is not
    // a connection, and an unsupplied basin cannot become a standing puddle.
    std::vector<uint32_t> queue;
    for (const auto& a : network.nodes) {
        if (r.streamLevels[a.cell] > f.heightmap.heights[a.cell] && !r.connected[a.cell]) {
            r.connected[a.cell] = 1; queue.push_back(a.cell);
        }
    }
    constexpr int offsets[6][2]{{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}};
    for (size_t k = 0; k < queue.size(); ++k) {
        uint32_t i = queue[k]; int x = int(i % n), z = int(i / n);
        for (auto& offset : offsets) {
            int xx = x + offset[0], zz = z + offset[1];
            if (xx < 0 || xx >= n || zz < 0 || zz >= n) continue;
            uint32_t j = uint32_t(zz) * n + xx;
            if (!r.connected[j] && r.streamLevels[j] > f.heightmap.heights[j]) {
                r.connected[j] = 1; queue.push_back(j);
            }
        }
    }
    r.surface.streamLevels_.resize(size_t(m) * m);
    r.surface.triangles_.resize(size_t(m - 1) * (m - 1) * 2);
    for (int z = 0; z < m; ++z)
        for (int x = 0; x < m; ++x)
            r.surface.streamLevels_[size_t(z) * m + x] = r.streamLevels[size_t(z + apron) * n + x + apron];
    for (int z = 0; z < n - 1; ++z) {
        for (int x = 0; x < n - 1; ++x) {
            bool playable = x >= apron && z >= apron && x < apron + m - 1 && z < apron + m - 1;
            auto quad = TerrainSurface::quadIndices(n, x, z);
            for (int t = 0; t < 2; ++t) {
                Polygon base;
                Surface::Triangle triangle;
                glm::vec2 fallback(0);
                for (int k = 0; k < 3; ++k) {
                    uint32_t i = quad[t * 3 + k];
                    base.push_back({position(i), f.heightmap.heights[i], r.streamLevels[i]});
                    triangle.stream |= r.connected[i] != 0;
                    fallback += guideFlow[i];
                    int32_t b = d.basin[i];
                    if (b >= 0 && lakes.lakes[b].present) {
                        if (triangle.lake >= 0 && triangle.lake != b) throw std::invalid_argument("incompatible lakes share water triangle");
                        triangle.lake = b; triangle.lakeLevel = lakes.lakes[b].level;
                    }
                }
                auto a = glm::dvec3(base[0].xz.x, base[0].stream, base[0].xz.y);
                auto b = glm::dvec3(base[1].xz.x, base[1].stream, base[1].xz.y);
                auto c = glm::dvec3(base[2].xz.x, base[2].stream, base[2].xz.y);
                auto normal = glm::normalize(glm::cross(b - a, c - a));
                glm::dvec2 downhill(normal.x, normal.z);
                double slopeLength = glm::length(downhill);
                triangle.flow = slopeLength > 0 ? glm::vec2(downhill / slopeLength) :
                    (glm::length(fallback) > 0 ? glm::normalize(fallback) : glm::vec2(0));
                if (playable) r.surface.triangles_[2 * (size_t(z - apron) * (m - 1) + x - apron) + t] = triangle;
                Polygon stream, lake;
                if (triangle.stream && triangle.lake >= 0) {
                    double low = std::min({base[0].stream, base[1].stream, base[2].stream});
                    double high = std::max({base[0].stream, base[1].stream, base[2].stream});
                    if (high <= triangle.lakeLevel) lake = base;
                    else if (low >= triangle.lakeLevel) stream = base;
                    else {
                        stream = clip(base, Cut::AboveLake, triangle.lakeLevel);
                        lake = clip(base, Cut::BelowLake, triangle.lakeLevel);
                    }
                } else if (triangle.stream) stream = base;
                else if (triangle.lake >= 0) lake = base;
                auto emit = [&](Polygon polygon, bool moving) {
                    polygon = clip(polygon, moving ? Cut::WetStream : Cut::WetLake, triangle.lakeLevel);
                    if (polygon.size() < 3) return;
                    auto height = [&](const Point& p) { return moving ? p.stream : double(triangle.lakeLevel); };
                    bool wet = std::any_of(polygon.begin(), polygon.end(), [&](const Point& p) { return height(p) > p.ground; });
                    if (!wet) return;
                    for (size_t k = 0; k < polygon.size(); ++k) {
                        const auto& p = polygon[k]; const auto& q = polygon[(k + 1) % polygon.size()];
                        if (height(p) == p.ground && height(q) == q.ground && p.xz != q.xz)
                            shores.push_back({p.xz, q.xz});
                    }
                    for (size_t k = 1; k + 1 < polygon.size(); ++k) {
                        double size = area(polygon[0], polygon[k], polygon[k + 1]);
                        if (size <= 0) continue;
                        (moving ? r.streamArea : r.lakeArea) += size;
                        if (!playable) continue;
                        ++(moving ? r.streamTriangles : r.lakeTriangles);
                        for (size_t v : {size_t(0), k, k + 1}) {
                            const auto& p = polygon[v];
                            r.surface.mesh_.indices.push_back(uint32_t(r.surface.mesh_.vertices.size()));
                            r.surface.mesh_.vertices.push_back({{p.xz.x, float(height(p)), p.xz.y},
                                moving ? glm::vec3(normal) : glm::vec3(0, 1, 0), moving ? triangle.flow : glm::vec2(0),
                                float(std::max(0.0, height(p) - p.ground))});
                        }
                    }
                };
                emit(std::move(stream), true); emit(std::move(lake), false);
            }
        }
    }
    r.surface.shoreline_ = ShorelineIndex(std::move(shores));
    r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return r;
}
} // namespace TerrainWater
