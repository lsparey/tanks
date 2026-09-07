#include "scene/TerrainGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void close(double a, double b, double tolerance, const char* message) {
    require(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance, message);
}
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid bank survey accepted");
}
MacroTerrain::Fields field(int n) {
    MacroTerrain::Fields f;
    f.playableResolution = n; f.playableWorldSize = float(n - 1); f.spacing = 1;
    f.heightmap = {n, float(n - 1), std::vector<float>(size_t(n) * n)};
    return f;
}
StreamNetwork::Result edge(const MacroTerrain::Fields& f, int x, int z, int dx, int dz, float level) {
    StreamNetwork::Result r;
    for (int i = 0; i < 2; ++i) {
        StreamNetwork::Node node;
        node.cell = (z + i * dz) * f.heightmap.resolution + x + i * dx;
        node.position = {(float(x + i * dx - f.apronCells) / (f.playableResolution - 1) - .5f) * f.playableWorldSize,
                         (float(z + i * dz - f.apronCells) / (f.playableResolution - 1) - .5f) * f.playableWorldSize};
        node.ground = f.heightmap.heights[node.cell];
        node.waterLevel = level; node.width = 1;
        node.downstream = i == 0 ? 1 : -1;
        r.nodes.push_back(node);
    }
    r.downstreamOrder = {1, 0};
    return r;
}
void check(const MacroTerrain::Fields& f, const StreamNetwork::Result& network, const StreamSections::Result& r) {
    using End = StreamSections::End;
    TerrainSurface surface(f.crop());
    size_t edges = std::count_if(network.nodes.begin(), network.nodes.end(), [](const auto& n) { return n.downstream >= 0; });
    require(r.sections.size() == edges * 3, "edge stations missing");
    for (const auto& s : r.sections) {
        close(glm::length(s.leftDirection), 1, 1e-12, "section width direction is not unit length");
        auto tangent = glm::dvec2(network.nodes[s.to].position) - glm::dvec2(network.nodes[s.from].position);
        close(glm::dot(tangent, s.leftDirection), 0, 1e-12, "section is not perpendicular to its incident edge");
        for (int side = 0; side < 2; ++side) {
            const auto& b = side == 0 ? s.left : s.right;
            auto direction = side == 0 ? s.leftDirection : -s.leftDirection;
            require(b.count >= 1 && size_t(b.first) + b.count <= r.points.size(), "invalid bank point slice");
            close(r.points[b.first].distance, 0, 0, "bank omits centre");
            const auto& end = r.points[b.first + b.count - 1];
            close(end.distance, b.distance, 0, "bank distance does not match endpoint");
            if (b.end == End::Shore) close(end.ground, s.waterLevel, 0, "bank endpoint floats above or below terrain");
            require(b.area >= 0 && b.wettedPerimeter >= b.distance, "invalid cross-section integral");
            for (uint32_t k = 1; k < b.count; ++k) {
                const auto& a = r.points[b.first + k - 1];
                const auto& p = r.points[b.first + k];
                require(p.distance > a.distance && p.ground <= s.waterLevel, "cross-section jumps dry ground");
                // Independent authoritative height queries between each retained
                // break catch a missed diagonal, even when endpoints happen to agree.
                for (double t : {.2, .5, .8}) {
                    auto q = s.centre + direction * std::lerp(a.distance, p.distance, t);
                    double half = f.playableWorldSize * .5;
                    if (std::abs(q.x) < half && std::abs(q.y) < half)
                        close(surface.heightAt(float(q.x), float(q.y)), std::lerp(a.ground, p.ground, t), 3e-5,
                              "section cuts across a terrain triangle without recording its edge");
                }
            }
        }
    }
    size_t control = 0;
    for (uint32_t i = 0; i < network.nodes.size(); ++i) {
        const auto& a = network.nodes[i];
        if (a.downstream < 0) continue;
        const auto& b = network.nodes[a.downstream];
        bool dryA = a.ground == a.waterLevel, dryB = b.ground == b.waterLevel;
        if (!dryA && !dryB) continue;
        require(control < r.controls.size(), "unreported zero-depth connection");
        const auto& c = r.controls[control++];
        require(c.from == i && c.to == uint32_t(a.downstream) && c.dryFrom == dryA && c.dryTo == dryB,
                "incorrect spill control");
    }
    require(control == r.controls.size(), "spurious spill control");
    require(r.boundedSections == std::count_if(r.sections.begin(), r.sections.end(), [](const auto& s) { return s.bounded(); }),
            "bounded section summary disagrees with geometry");
}
void same(const StreamSections::Result& a, const StreamSections::Result& b) {
    require(a.sections.size() == b.sections.size() && a.points.size() == b.points.size() &&
            a.controls.size() == b.controls.size(), "nondeterministic section counts");
    for (size_t i = 0; i < a.points.size(); ++i)
        require(a.points[i].ground == b.points[i].ground && a.points[i].distance == b.points[i].distance,
                "nondeterministic bank points");
    for (size_t i = 0; i < a.controls.size(); ++i) {
        const auto& x = a.controls[i]; const auto& y = b.controls[i];
        require(x.from == y.from && x.to == y.to && x.dryFrom == y.dryFrom && x.dryTo == y.dryTo,
                "nondeterministic spill controls");
    }
    for (size_t i = 0; i < a.sections.size(); ++i) {
        const auto& x = a.sections[i]; const auto& y = b.sections[i];
        require(x.from == y.from && x.to == y.to && x.station == y.station && x.centre == y.centre &&
                x.leftDirection == y.leftDirection && x.waterLevel == y.waterLevel && x.ground == y.ground &&
                x.requestedWidth == y.requestedWidth, "nondeterministic section geometry");
        for (auto banks : {std::pair{&x.left, &y.left}, std::pair{&x.right, &y.right}}) {
            const auto& p = *banks.first; const auto& q = *banks.second;
            require(p.end == q.end && p.first == q.first && p.count == q.count && p.distance == q.distance &&
                    p.area == q.area && p.wettedPerimeter == q.wettedPerimeter, "nondeterministic bank integral");
        }
    }
}
}

int main() {
    using End = StreamSections::End;
    // Analytic asymmetric V: right rises twice as quickly as left. Horizontal
    // water at height one gives half-widths .5 and 1, area .75.
    auto f = field(9);
    for (int z = 0; z < 9; ++z)
        for (int x = 0; x < 9; ++x) f.heightmap.heights[z * 9 + x] = z < 4 ? float(4 - z) : 2.0f * (z - 4);
    auto n = edge(f, 4, 4, -1, 0, 1);
    auto r = StreamSections::build(f, n);
    check(f, n, r);
    require(r.boundedSections == 3 && r.controls.empty(), "analytic channel is not bounded");
    for (const auto& s : r.sections) {
        close(s.left.distance, 1, 1e-6, "incorrect left bank");
        close(s.right.distance, .5, 1e-6, "incorrect right bank");
        close(s.left.area + s.right.area, .75, 1e-6, "incorrect channel area");
        close(s.left.wettedPerimeter + s.right.wettedPerimeter, std::sqrt(2.) + std::sqrt(1.25), 1e-6,
              "incorrect wetted perimeter");
    }
    // A dry ridge separates two wet depressions. Stop at its FIRST shore.
    f.heightmap.heights[6 * 9 + 4] = -5;
    auto ridge = StreamSections::build(f, n);
    close(ridge.sections[0].right.distance, .5, 1e-6, "survey skipped a dry separating ridge");
    // Flat ground has no banks: neither a search radius nor a crop/domain edge
    // is permission to terminate a rendered water ribbon above dry ground.
    auto flat = field(9);
    auto fn = edge(flat, 4, 4, -1, 0, 1);
    auto limited = StreamSections::build(flat, fn, {.searchDistance = 1});
    require(limited.searchLimitedSections == 3 && limited.boundedSections == 0, "invented search-limit banks");
    close(limited.sections[0].left.area, 1, 1e-12, "truncated wetted area is incorrect");
    auto full = StreamSections::build(flat, fn);
    require(full.domainLimitedSections == 3, "invented domain-edge banks");
    auto crop = flat; crop.apronCells = 2; crop.playableResolution = 5; crop.playableWorldSize = 4;
    auto cropped = StreamSections::build(crop, edge(crop, 4, 4, -1, 0, 1));
    close(cropped.sections[0].left.distance, 4, 0, "playable crop was treated as a bank");
    auto boundary = StreamSections::build(flat, edge(flat, 0, 0, 1, 0, 1));
    require(boundary.sections[0].right.end == End::DomainEdge && boundary.sections[0].right.distance == 0,
            "outward ray at boundary was clamped into an artificial bank");

    // NW/SE route; its cross-section crosses the mesh diagonal inside quads.
    // High opposite corners must be sampled, not bilinearly smoothed away.
    auto saddle = field(9);
    for (int z = 0; z < 9; ++z)
        for (int x = 0; x < 9; ++x) saddle.heightmap.heights[z * 9 + x] = float((x - z) * (x - z));
    auto sn = edge(saddle, 4, 4, 1, 1, 1.5f);
    auto sr = StreamSections::build(saddle, sn);
    check(saddle, sn, sr);
    require(sr.boundedSections == 3, "diagonal valley lost its banks");
    // Numerical integration of independently sampled terrain also checks area.
    for (const auto& s : sr.sections) {
        TerrainSurface surface(saddle.crop());
        double area = 0, distance = s.left.distance;
        for (int k = 0; k < 10000; ++k) {
            auto p = s.centre + s.leftDirection * (distance * (k + .5) / 10000);
            area += (s.waterLevel - surface.heightAt(float(p.x), float(p.y))) * distance / 10000;
        }
        close(s.left.area, area, 1e-6, "diagonal wet area disagrees with independent quadrature");
    }
    auto dry = edge(flat, 4, 4, -1, 0, 0);
    auto dr = StreamSections::build(flat, dry);
    require(dr.drySections == 3 && dr.controls.size() == 1 && dr.controls[0].dryFrom && dr.controls[0].dryTo,
            "flat spill span became fictitious water");
    dry.nodes[0].waterLevel = .25f;
    auto pinch = StreamSections::build(flat, dry);
    require(pinch.drySections == 1 && pinch.controls.size() == 1 && !pinch.controls[0].dryFrom && pinch.controls[0].dryTo,
            "point spill pinch was not distinguished from dry span");
    for (int bad = 0; bad < 8; ++bad) {
        auto ff = flat; auto nn = fn; StreamSections::Settings settings;
        if (bad == 0) settings.searchDistance = std::numeric_limits<float>::quiet_NaN();
        if (bad == 1) settings.searchDistance = 0;
        if (bad == 2) ff.heightmap.heights.pop_back();
        if (bad == 3) ff.heightmap.heights[0] = std::numeric_limits<float>::infinity();
        if (bad == 4) nn.nodes[0].position.x += 1;
        if (bad == 5) nn.nodes[0].ground += 1;
        if (bad == 6) nn.nodes[0].downstream = 100;
        if (bad == 7) nn.downstreamOrder = {0, 0};
        rejects([&] { StreamSections::build(ff, nn, settings); });
    }
    TerrainGenerator::Settings settings;
    settings.streamSections.emplace();
    rejects([&] { TerrainGenerator::build(settings); });
    settings.preset = TerrainGenerator::Preset::DrainedValley;
    settings.lakes.emplace(); settings.streams.emplace(); settings.resolution = 33;
    settings.erosion.duration = .25; settings.erosion.rainDuration = .15; settings.erosion.talusPasses = 1;
    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        settings.seed = seed; settings.erosion.workers = 1;
        auto a = TerrainGenerator::build(settings);
        require(a.streamSections.has_value(), "generator omitted bank survey");
        check(*a.generationFields, *a.streams, *a.streamSections);
        settings.erosion.workers = 4;
        auto b = TerrainGenerator::build(settings);
        same(*a.streamSections, *b.streamSections);
    }
}
