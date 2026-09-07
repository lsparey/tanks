#include "StreamSections.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace StreamSections {
namespace {
// Anchor full-domain positions to the playable mesh, as in LakeWater and
// StreamNetwork. Work in double over those exact float vertex positions.
struct Ground {
    const MacroTerrain::Fields& fields;
    std::vector<double> coordinates;
    int cell(double p) const {
        return std::clamp(int(std::upper_bound(coordinates.begin(), coordinates.end(), p) - coordinates.begin()) - 1,
                          0, int(coordinates.size()) - 2);
    }
    double height(glm::dvec2 p) const {
        int x = cell(p.x), z = cell(p.y), n = fields.heightmap.resolution;
        double tx = std::clamp((p.x - coordinates[x]) / (coordinates[x + 1] - coordinates[x]), 0.0, 1.0);
        double tz = std::clamp((p.y - coordinates[z]) / (coordinates[z + 1] - coordinates[z]), 0.0, 1.0);
        const auto& h = fields.heightmap.heights;
        size_t a = size_t(z) * n + x;
        return tz >= tx ? h[a] * (1 - tz) + h[a + n] * (tz - tx) + h[a + n + 1] * tx :
                          h[a] * (1 - tx) + h[a + n + 1] * tz + h[a + 1] * (tx - tz);
    }
};

Bank survey(const Ground& ground, glm::dvec2 centre, glm::dvec2 direction,
            double level, double centreHeight, double searchDistance, std::vector<Point>& points) {
    Bank bank;
    bank.first = uint32_t(points.size());
    points.push_back({0, centreHeight});
    bank.count = 1;
    if (centreHeight >= level) return bank;

    double boundary = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 2; ++axis) {
        if (direction[axis] > 0) boundary = std::min(boundary, (ground.coordinates.back() - centre[axis]) / direction[axis]);
        if (direction[axis] < 0) boundary = std::min(boundary, (ground.coordinates.front() - centre[axis]) / direction[axis]);
    }
    double limit = std::max(0.0, std::min(searchDistance, boundary));
    bank.end = boundary <= searchDistance ? End::DomainEdge : End::SearchLimit;
    // Split first at vertical/horizontal grid lines. Between those crossings
    // the ray lies in ONE quad, whose real 00--11 diagonal adds at most one cut.
    std::vector<double> cuts{0, limit};
    for (int axis = 0; axis < 2; ++axis) {
        if (direction[axis] == 0) continue;
        double last = centre[axis] + direction[axis] * limit;
        auto begin = std::lower_bound(ground.coordinates.begin(), ground.coordinates.end(), std::min(centre[axis], last));
        auto end = std::upper_bound(begin, ground.coordinates.end(), std::max(centre[axis], last));
        for (auto it = begin; it != end; ++it) {
            double t = (*it - centre[axis]) / direction[axis];
            if (t > 0 && t < limit) cuts.push_back(t);
        }
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    size_t count = cuts.size();
    for (size_t i = 1; i < count; ++i) {
        auto mid = centre + direction * ((cuts[i - 1] + cuts[i]) * .5);
        int x = ground.cell(mid.x), z = ground.cell(mid.y);
        double sx = ground.coordinates[x + 1] - ground.coordinates[x];
        double sz = ground.coordinates[z + 1] - ground.coordinates[z];
        double rate = direction.x / sx - direction.y / sz;
        if (rate == 0) continue; // on or parallel to the real mesh diagonal
        double t = ((centre.y - ground.coordinates[z]) / sz - (centre.x - ground.coordinates[x]) / sx) / rate;
        if (t > cuts[i - 1] && t < cuts[i]) cuts.push_back(t);
    }
    std::sort(cuts.begin(), cuts.end());
    double previousT = 0, previousH = centreHeight;
    for (double t : cuts) {
        if (t <= previousT) continue;
        double h = ground.height(centre + direction * t);
        bool shore = h >= level;
        if (shore) {
            t = previousT + (t - previousT) * (level - previousH) / (h - previousH);
            h = level;
            bank.end = End::Shore;
        }
        double width = t - previousT;
        bank.area += width * ((level - previousH) + (level - h)) * .5;
        bank.wettedPerimeter += std::hypot(width, h - previousH);
        bank.distance = t;
        points.push_back({t, h});
        ++bank.count;
        previousT = t; previousH = h;
        if (shore) break;
    }
    return bank;
}
} // namespace

const char* endName(End end) {
    switch (end) {
        case End::Shore: return "shore";
        case End::DomainEdge: return "domain-edge";
        case End::SearchLimit: return "search-limit";
        case End::DryCentre: return "dry-centre";
    }
    throw std::invalid_argument("invalid bank endpoint");
}
void validate(const Settings& s) {
    if (!std::isfinite(s.searchDistance) || s.searchDistance < .01f || s.searchDistance > 1000)
        throw std::invalid_argument("bank search distance must be .01..1000 world units");
}
size_t Result::payloadBytes() const {
    return sections.capacity() * sizeof(Section) + points.capacity() * sizeof(Point) + controls.capacity() * sizeof(Control);
}
Result build(const MacroTerrain::Fields& f, const StreamNetwork::Result& network, const Settings& settings) {
    auto started = std::chrono::steady_clock::now();
    validate(settings);
    int n = f.heightmap.resolution, m = f.playableResolution, apron = f.apronCells;
    if (n < 2 || n > 4097 || m < 2 || m > n || apron < 0 || apron > n / 2 || m + 2 * apron != n ||
        f.heightmap.heights.size() != size_t(n) * n || !std::isfinite(f.playableWorldSize) || f.playableWorldSize <= 0 ||
        !std::isfinite(f.heightmap.worldSize) || f.heightmap.worldSize <= 0 || !std::isfinite(f.spacing) || f.spacing <= 0 ||
        std::abs(double(f.playableWorldSize) - double(f.spacing) * (m - 1)) > 1e-6 * f.playableWorldSize ||
        std::abs(double(f.heightmap.worldSize) - double(f.spacing) * (n - 1)) > 1e-6 * f.heightmap.worldSize)
        throw std::invalid_argument("invalid bank survey domain");
    Ground ground{f, {}};
    for (int i = 0; i < n; ++i) {
        double p = (float(i - apron) / (m - 1) - .5f) * f.playableWorldSize;
        if (!std::isfinite(p) || (!ground.coordinates.empty() && p <= ground.coordinates.back()))
            throw std::invalid_argument("unrepresentable bank survey coordinates");
        ground.coordinates.push_back(p);
    }
    for (float h : f.heightmap.heights)
        if (!std::isfinite(h) || std::abs(h) > 1e6f) throw std::invalid_argument("invalid bank survey height");
    std::vector<uint32_t> rank(network.nodes.size(), uint32_t(network.nodes.size()));
    if (network.downstreamOrder.size() != rank.size()) throw std::invalid_argument("invalid bank survey network order");
    for (uint32_t k = 0; k < rank.size(); ++k) {
        uint32_t i = network.downstreamOrder[k];
        if (i >= rank.size() || rank[i] != rank.size()) throw std::invalid_argument("invalid bank survey network order");
        rank[i] = k;
    }
    for (uint32_t i = 0; i < network.nodes.size(); ++i) {
        const auto& a = network.nodes[i];
        if (a.cell >= size_t(n) * n || a.ground != f.heightmap.heights[a.cell] ||
            !std::isfinite(a.waterLevel) || a.waterLevel < a.ground || std::abs(a.waterLevel) > 1e6 ||
            !std::isfinite(a.width) || a.width <= 0 || a.width > 100 ||
            glm::dvec2(a.position) != glm::dvec2(ground.coordinates[a.cell % n], ground.coordinates[a.cell / n]) ||
            a.downstream < -1 || (a.downstream >= 0 && (size_t(a.downstream) >= rank.size() || rank[a.downstream] >= rank[i])))
            throw std::invalid_argument("invalid or stale bank survey network");
        if (a.downstream >= 0) {
            const auto& b = network.nodes[a.downstream];
            int dx = int(a.cell % n) - int(b.cell % n), dz = int(a.cell / n) - int(b.cell / n);
            if (!((std::abs(dx) + std::abs(dz) == 1) || (dx == dz && std::abs(dx) == 1)) || a.waterLevel < b.waterLevel)
                throw std::invalid_argument("bank survey requires descending terrain-edge profiles");
        }
    }
    Result r;
    for (uint32_t i = 0; i < network.nodes.size(); ++i) {
        const auto& a = network.nodes[i];
        if (a.downstream < 0) continue;
        uint32_t j = uint32_t(a.downstream);
        const auto& b = network.nodes[j];
        bool dryA = a.waterLevel == a.ground, dryB = b.waterLevel == b.ground;
        if (dryA || dryB) r.controls.push_back({i, j, dryA, dryB});
        auto tangent = glm::normalize(glm::dvec2(b.position) - glm::dvec2(a.position));
        for (double t : {0.0, .5, 1.0}) {
            Section s;
            s.from = i; s.to = j; s.station = t;
            s.centre = glm::mix(glm::dvec2(a.position), glm::dvec2(b.position), t);
            s.leftDirection = {-tangent.y, tangent.x};
            s.waterLevel = std::lerp(double(a.waterLevel), double(b.waterLevel), t);
            // Selected edges are terrain edges: interpolating endpoints avoids
            // turning roundoff at a zero-depth spill into a fictitious wet film.
            s.ground = std::lerp(double(a.ground), double(b.ground), t);
            s.requestedWidth = std::lerp(double(a.width), double(b.width), t);
            s.left = survey(ground, s.centre, s.leftDirection, s.waterLevel, s.ground, settings.searchDistance, r.points);
            s.right = survey(ground, s.centre, -s.leftDirection, s.waterLevel, s.ground, settings.searchDistance, r.points);
            r.boundedSections += s.bounded();
            r.drySections += s.left.end == End::DryCentre;
            r.domainLimitedSections += s.left.end == End::DomainEdge || s.right.end == End::DomainEdge;
            r.searchLimitedSections += s.left.end == End::SearchLimit || s.right.end == End::SearchLimit;
            r.sections.push_back(s);
        }
    }
    r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return r;
}
} // namespace StreamSections
