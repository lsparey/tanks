#include "ChannelCarving.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ChannelCarving {
namespace {
float roundUp(double value) {
    float rounded = float(value);
    return double(rounded) < value ? std::nextafter(rounded, std::numeric_limits<float>::infinity()) : rounded;
}
}
void validate(const Settings& s) {
    if (!std::isfinite(s.maximumCut) || s.maximumCut <= 0 || s.maximumCut > 2)
        throw std::invalid_argument("maximum channel cut must be greater than zero and at most 2 world units");
}
size_t Result::payloadBytes() const {
    return cutDepth.capacity() * sizeof(float) + protectedCells.capacity() * sizeof(uint8_t);
}
Result apply(MacroTerrain::Fields& f, const TerrainDrainage::Result& d,
             const StreamNetwork::Result& network, const Settings& settings) {
    auto started = std::chrono::steady_clock::now();
    validate(settings);
    int n = f.heightmap.resolution, m = f.playableResolution, apron = f.apronCells;
    if (n < 2 || n > 4097 || m < 2 || m > n || apron < 0 || apron > n / 2 || m + 2 * apron != n ||
        !std::isfinite(f.spacing) || f.spacing <= 0 || !std::isfinite(f.playableWorldSize) || f.playableWorldSize <= 0 ||
        !std::isfinite(f.heightmap.worldSize) || f.heightmap.worldSize <= 0 ||
        std::abs(double(f.spacing) * (m - 1) - f.playableWorldSize) > f.playableWorldSize * 1e-6 ||
        std::abs(double(f.spacing) * (n - 1) - f.heightmap.worldSize) > f.heightmap.worldSize * 1e-6)
        throw std::invalid_argument("invalid channel carving domain");
    size_t count = size_t(n) * n;
    if (f.heightmap.heights.size() != count || f.bedrock.size() != count || f.soil.size() != count ||
        f.erodibility.size() != count || f.openFaces.size() != count || d.basin.size() != count ||
        d.downstream.size() != count || d.order.size() != count || d.spillElevation.size() != count)
        throw std::invalid_argument("channel carving field dimensions disagree");
    const auto& original = f.heightmap.heights;
    std::vector<double> coordinates;
    for (int i = 0; i < n; ++i) {
        double p = (float(i - apron) / (m - 1) - .5f) * f.playableWorldSize;
        if (!std::isfinite(p) || (!coordinates.empty() && p <= coordinates.back()))
            throw std::invalid_argument("unrepresentable channel coordinates");
        coordinates.push_back(p);
    }
    std::vector<uint32_t> rank(count, uint32_t(count));
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t i = d.order[k];
        if (i >= count || rank[i] != count) throw std::invalid_argument("invalid carving drainage order");
        rank[i] = k;
    }
    for (uint32_t i = 0; i < count; ++i) {
        int32_t j = d.downstream[i], b = d.basin[i];
        if (!std::isfinite(original[i]) || std::abs(original[i]) > 1e6 || !std::isfinite(f.bedrock[i]) ||
            std::abs(f.bedrock[i]) > 1e6 || !std::isfinite(f.soil[i]) || f.soil[i] < 0 || f.soil[i] > 1e6 ||
            original[i] != f.bedrock[i] + f.soil[i] || !std::isfinite(f.erodibility[i]) || f.erodibility[i] < 0 || f.erodibility[i] > 1 ||
            !std::isfinite(d.spillElevation[i]) || d.spillElevation[i] < original[i] || b < -1 ||
            (b >= 0 && size_t(b) >= d.basins.size()) || (b >= 0) != (original[i] < d.spillElevation[i]) ||
            j < -1 || (j >= 0 && (size_t(j) >= count || rank[j] >= rank[i] || d.spillElevation[j] > d.spillElevation[i])) ||
            (j < 0) != (f.openFaces[i] != 0))
            throw std::invalid_argument("invalid or stale channel material/drainage fields");
        if (j >= 0) {
            int dx = int(i % n) - j % n, dz = int(i / n) - j / n;
            if (!((std::abs(dx) + std::abs(dz) == 1) || (dx == dz && std::abs(dx) == 1)))
                throw std::invalid_argument("channel drainage is not on mesh edges");
        }
    }
    std::vector<int32_t> cellNode(count, -1);
    if (network.downstreamOrder.size() != network.nodes.size()) throw std::invalid_argument("invalid channel network order");
    std::vector<uint32_t> nodeRank(network.nodes.size(), uint32_t(network.nodes.size()));
    for (uint32_t k = 0; k < network.nodes.size(); ++k) {
        uint32_t i = network.downstreamOrder[k];
        if (i >= nodeRank.size() || nodeRank[i] != nodeRank.size()) throw std::invalid_argument("invalid channel network order");
        nodeRank[i] = k;
    }
    for (uint32_t i = 0; i < network.nodes.size(); ++i) {
        const auto& a = network.nodes[i];
        int32_t j = a.downstream;
        if (a.cell >= count || a.ground != original[a.cell] ||
            glm::dvec2(a.position) != glm::dvec2(coordinates[a.cell % n], coordinates[a.cell / n]) ||
            !std::isfinite(a.width) || a.width <= 0 || a.width > 100 ||
            !std::isfinite(a.requestedDepth) || a.requestedDepth <= 0 || a.requestedDepth > 10 ||
            j < -1 || (j >= 0 && (size_t(j) >= nodeRank.size() || nodeRank[j] >= nodeRank[i] ||
                                  d.downstream[a.cell] != int32_t(network.nodes[j].cell))))
            throw std::invalid_argument("invalid or stale channel profile");
        // Reservoir inlet/outlet nodes may share a cell. Dry channel vertices
        // must be unique so downstream grading has one authoritative target.
        if (d.basin[a.cell] < 0) {
            if (cellNode[a.cell] >= 0) throw std::invalid_argument("duplicate dry channel vertex");
            cellNode[a.cell] = int32_t(i);
        }
    }
    Result r;
    r.protectedCells.resize(count);
    r.cutDepth.resize(count);
    constexpr int offsets[6][2]{{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}};
    for (uint32_t i = 0; i < count; ++i) {
        if (d.basin[i] < 0) continue;
        r.protectedCells[i] = 1;
        int x = int(i % n), z = int(i / n);
        for (auto& offset : offsets) {
            int xx = x + offset[0], zz = z + offset[1];
            if (xx >= 0 && xx < n && zz >= 0 && zz < n) r.protectedCells[size_t(zz) * n + xx] = 1;
        }
    }
    // Target the bed BELOW the old ground, not below the old backwater guide.
    // Grading against downstream controls avoids excavating a trench that ends
    // below an untouched lake rim. Protected reservoirs retain their old bed.
    std::vector<double> bed(network.nodes.size());
    for (uint32_t i : network.downstreamOrder) {
        const auto& a = network.nodes[i];
        bed[i] = a.ground;
        if (r.protectedCells[a.cell]) continue;
        bed[i] -= std::min(a.requestedDepth, settings.maximumCut);
        if (a.downstream >= 0) bed[i] = std::max(bed[i], bed[a.downstream]);
        bed[i] = std::min(bed[i], double(a.ground));
    }
    auto targets = original;
    for (uint32_t i = 0; i < network.nodes.size(); ++i) {
        const auto& a = network.nodes[i];
        if (a.downstream < 0) continue;
        uint32_t j = uint32_t(a.downstream);
        const auto& b = network.nodes[j];
        auto p = glm::dvec2(a.position), q = glm::dvec2(b.position), edge = q - p;
        double lengthSquared = glm::dot(edge, edge), radius = std::max(a.width, b.width) * .5;
        int xmin = int(std::lower_bound(coordinates.begin(), coordinates.end(), std::min(p.x, q.x) - radius) - coordinates.begin());
        int xmax = int(std::upper_bound(coordinates.begin(), coordinates.end(), std::max(p.x, q.x) + radius) - coordinates.begin());
        int zmin = int(std::lower_bound(coordinates.begin(), coordinates.end(), std::min(p.y, q.y) - radius) - coordinates.begin());
        int zmax = int(std::upper_bound(coordinates.begin(), coordinates.end(), std::max(p.y, q.y) + radius) - coordinates.begin());
        for (int z = zmin; z < zmax; ++z) {
            for (int x = xmin; x < xmax; ++x) {
                uint32_t cell = uint32_t(z) * n + x;
                if (r.protectedCells[cell]) continue;
                glm::dvec2 at(coordinates[x], coordinates[z]);
                double t = std::clamp(glm::dot(at - p, edge) / lengthSquared, 0.0, 1.0);
                double halfWidth = std::lerp(double(a.width), double(b.width), t) * .5;
                auto delta = at - (p + t * edge);
                double relativeSquared = glm::dot(delta, delta) / (halfWidth * halfWidth);
                if (relativeSquared >= 1) continue;
                double rim = std::lerp(double(a.ground), double(b.ground), t);
                double depth = rim - std::lerp(bed[i], bed[j], t);
                if (depth <= 0) continue;
                double target = std::max(double(original[cell]) - settings.maximumCut, rim - depth * (1 - relativeSquared));
                targets[cell] = std::min(targets[cell], roundUp(target));
            }
        }
    }
    // Overlapping bends/tributaries can cut a centre deeper than its own
    // profile. Restore as much of that proposed cut as needed to retain the
    // original descending dry graph. This never adds ground above the input.
    auto soil = f.soil, rock = f.bedrock, heights = original;
    for (uint32_t cell : d.order) {
        int32_t downstream = d.downstream[cell];
        if (cellNode[cell] >= 0 && downstream >= 0 && d.basin[cell] < 0)
            targets[cell] = std::min(original[cell], std::max(targets[cell], heights[downstream]));
        float target = targets[cell];
        if (target >= original[cell]) continue;
        if (target >= rock[cell]) soil[cell] = std::min(soil[cell], roundUp(double(target) - rock[cell]));
        else { soil[cell] = 0; rock[cell] = target; }
        heights[cell] = rock[cell] + soil[cell];
        if (heights[cell] > original[cell] || heights[cell] < target ||
            double(original[cell]) - heights[cell] > settings.maximumCut)
            throw std::runtime_error("channel material rounding violated the cut bound");
    }
    for (uint32_t i = 0; i < count; ++i) {
        int x = int(i % n), z = int(i / n);
        double area = double(f.spacing) * f.spacing * ((x == 0 || x == n - 1) ? .5 : 1) * ((z == 0 || z == n - 1) ? .5 : 1);
        r.budget.initialSoil += f.soil[i] * area;
        r.budget.finalSoil += soil[i] * area;
        r.budget.exportedSoil += (double(f.soil[i]) - soil[i]) * area;
        r.budget.exportedBedrock += (double(f.bedrock[i]) - rock[i]) * area;
        double removed = double(original[i]) - heights[i];
        r.cutDepth[i] = float(removed);
        r.changedCells += removed > 0;
        r.budget.removedGround += removed * area;
    }
    r.budget.materialResidual = r.budget.finalSoil + r.budget.exportedSoil - r.budget.initialSoil;
    r.budget.surfaceRoundingDelta = r.budget.removedGround - r.budget.exportedSoil - r.budget.exportedBedrock;
    f.heightmap.heights = std::move(heights);
    f.soil = std::move(soil);
    f.bedrock = std::move(rock);
    r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return r;
}
} // namespace ChannelCarving
