#include "TerrainDrainage.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace TerrainDrainage {
namespace {
// TerrainSurface::quadIndices connects 00 to 11, never 01 to 10.
constexpr std::array<int, 6> kDx{-1, 1, 0, 0, -1, 1};
constexpr std::array<int, 6> kDz{0, 0, -1, 1, -1, 1};
struct Node { float height; uint32_t index, sequence; };
struct Later {
    bool operator()(const Node& a, const Node& b) const {
        return a.height > b.height || (a.height == b.height && a.sequence > b.sequence);
    }
};
double areaAt(int n, double spacing, uint32_t i) {
    int x = int(i % n), z = int(i / n);
    return spacing * spacing * (x == 0 || x == n - 1 ? .5 : 1) * (z == 0 || z == n - 1 ? .5 : 1);
}
}

size_t Result::payloadBytes() const {
    return spillElevation.capacity() * sizeof(float) +
        (downstream.capacity() + basin.capacity()) * sizeof(int32_t) +
        (order.capacity() + outlet.capacity()) * sizeof(uint32_t) +
        (contributingArea.capacity() + runoff.capacity()) * sizeof(double) + basins.capacity() * sizeof(Basin);
}

Result analyze(const MacroTerrain::Fields& fields, const Settings& settings) {
    auto started = std::chrono::steady_clock::now();
    const auto& hm = fields.heightmap;
    int n = hm.resolution;
    if (n < 2 || n > 4097 || !std::isfinite(fields.spacing) || fields.spacing < .0001f ||
        fields.spacing > 1000000 || !std::isfinite(hm.worldSize) || hm.worldSize <= 0 ||
        std::abs(double(hm.worldSize) - double(fields.spacing) * (n - 1)) >
            1e-6 * std::max(1.0, double(hm.worldSize)))
        throw std::invalid_argument("invalid drainage grid");
    const size_t count = size_t(n) * n;
    if (hm.heights.size() != count || fields.openFaces.size() != count)
        throw std::invalid_argument("drainage field dimensions disagree");
    if (!std::isfinite(settings.rainfall) || settings.rainfall < 0 || settings.rainfall > 1 ||
        !std::isfinite(settings.infiltration) || settings.infiltration < 0 || settings.infiltration > 1)
        throw std::invalid_argument("invalid drainage rainfall or infiltration");
    bool hasOutlet = false;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            size_t i = size_t(z) * n + x;
            if (!std::isfinite(hm.heights[i])) throw std::invalid_argument("non-finite drainage height");
            int allowed = (x == 0 ? MacroTerrain::NegativeX : 0) | (x == n - 1 ? MacroTerrain::PositiveX : 0) |
                (z == 0 ? MacroTerrain::NegativeZ : 0) | (z == n - 1 ? MacroTerrain::PositiveZ : 0);
            if (fields.openFaces[i] & ~allowed) throw std::invalid_argument("drainage outlet is not an exterior face");
            hasOutlet |= fields.openFaces[i] != 0;
        }
    }
    if (!hasOutlet) throw std::invalid_argument("drainage requires an exterior outlet");

    Result r;
    r.spillElevation.resize(count);
    r.downstream.resize(count, -2); // -2 is unvisited
    r.order.reserve(count);
    r.outlet.resize(count);
    r.contributingArea.resize(count);
    r.runoff.resize(count);
    r.basin.resize(count, -1);
    std::vector<Node> heap;
    std::vector<uint32_t> rank(count), component;
    uint32_t sequence = 0;
    auto push = [&](uint32_t i) {
        heap.push_back({r.spillElevation[i], i, sequence++});
        std::push_heap(heap.begin(), heap.end(), Later{});
    };
    auto neighbour = [&](uint32_t i, int d) -> int32_t {
        int x = int(i % n) + kDx[d], z = int(i / n) + kDz[d];
        return x >= 0 && x < n && z >= 0 && z < n ? z * n + x : -1;
    };
    for (uint32_t i = 0; i < count; ++i) {
        if (!fields.openFaces[i]) continue;
        r.spillElevation[i] = hm.heights[i];
        r.downstream[i] = -1;
        push(i);
    }
    // Flood from known outlets. The minimax escape elevation is max(ground,
    // parent's escape level). A discovered vertex's parent is already popped,
    // so even equal-height flats have a strict, reproducible routing order.
    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), Later{});
        uint32_t i = heap.back().index;
        heap.pop_back();
        rank[i] = uint32_t(r.order.size());
        r.order.push_back(i);
        for (int d = 0; d < 6; ++d) {
            int32_t j = neighbour(i, d);
            if (j < 0 || r.downstream[j] != -2) continue;
            r.downstream[j] = int32_t(i);
            r.spillElevation[j] = std::max(hm.heights[j], r.spillElevation[i]);
            push(uint32_t(j));
        }
    }
    const double effectiveRain = std::max(0.0, settings.rainfall - settings.infiltration);
    for (uint32_t i : r.order) {
        if (r.downstream[i] >= 0) {
            // Prefer the steepest descending mesh edge on slopes. Retain the
            // flood parent on flats: no epsilon height changes or cycles.
            double steepest = 0;
            for (int d = 0; d < 6; ++d) {
                int32_t j = neighbour(i, d);
                if (j < 0 || rank[j] >= rank[i]) continue;
                double slope = (double(r.spillElevation[i]) - r.spillElevation[j]) /
                    (d < 4 ? 1.0 : std::sqrt(2.0));
                if (slope > steepest) {
                    steepest = slope;
                    r.downstream[i] = j;
                }
            }
            r.outlet[i] = r.outlet[r.downstream[i]];
        } else r.outlet[i] = i;
        double area = areaAt(n, fields.spacing, i);
        r.contributingArea[i] = area;
        r.runoff[i] = effectiveRain * area;
        r.domainArea += area;
        r.generatedRunoff += r.runoff[i];
    }
    for (auto it = r.order.rbegin(); it != r.order.rend(); ++it) {
        uint32_t i = *it;
        int32_t j = r.downstream[i];
        if (j >= 0) {
            r.contributingArea[j] += r.contributingArea[i];
            r.runoff[j] += r.runoff[i];
        } else r.outletRunoff += r.runoff[i];
    }
    r.runoffResidual = r.outletRunoff - r.generatedRunoff;

    // Label submerged components without modifying the ground or calling the
    // analysis surface permanent water. Nested partial lakes need a later
    // supply/loss and sub-basin policy.
    for (uint32_t start = 0; start < count; ++start) {
        if (r.basin[start] >= 0 || hm.heights[start] >= r.spillElevation[start]) continue;
        int32_t label = int32_t(r.basins.size());
        Basin b;
        b.spillElevation = r.spillElevation[start];
        b.minimumGround = hm.heights[start];
        component.clear();
        component.push_back(start);
        r.basin[start] = label;
        for (size_t c = 0; c < component.size(); ++c) {
            uint32_t i = component[c];
            double area = areaAt(n, fields.spacing, i);
            b.minimumGround = std::min(b.minimumGround, hm.heights[i]);
            b.area += area;
            b.storageToSpill += (double(b.spillElevation) - hm.heights[i]) * area;
            ++b.cells;
            for (int d = 0; d < 6; ++d) {
                int32_t j = neighbour(i, d);
                if (j < 0 || r.basin[j] >= 0 || r.spillElevation[j] != b.spillElevation ||
                    hm.heights[j] >= b.spillElevation) continue;
                r.basin[j] = label;
                component.push_back(uint32_t(j));
            }
        }
        r.basins.push_back(b);
    }
    for (uint32_t i : r.order) {
        if (r.basin[i] < 0) continue;
        int32_t j = r.downstream[i];
        if (j >= 0 && r.basin[j] == r.basin[i]) continue;
        auto& b = r.basins[r.basin[i]];
        if (b.outletLinks++ == 0) { b.spillFrom = int32_t(i); b.spillTo = j; }
    }
    r.peakWorkingBytes = r.payloadBytes() + heap.capacity() * sizeof(Node) +
        (rank.capacity() + component.capacity()) * sizeof(uint32_t);
    r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return r;
}

} // namespace TerrainDrainage
