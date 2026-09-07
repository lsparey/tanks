#include "scene/TerrainGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid playability input accepted");
}
MacroTerrain::Fields field(int n = 41) {
    MacroTerrain::Fields f;
    f.playableResolution = n; f.playableWorldSize = n - 1; f.spacing = 1;
    f.heightmap = {n, float(n - 1), std::vector<float>(size_t(n) * n)};
    f.openFaces.resize(size_t(n) * n);
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x)
        f.openFaces[size_t(z) * n + x] = (x == 0 ? 1 : 0) | (x == n - 1 ? 2 : 0) |
                                        (z == 0 ? 4 : 0) | (z == n - 1 ? 8 : 0);
    return f;
}
TerrainWater::Result water(const MacroTerrain::Fields& f) {
    auto d = TerrainDrainage::analyze(f);
    auto lakes = LakeWater::build(f, d);
    StreamNetwork::Settings s; s.minimumDischarge = 1e8;
    auto network = StreamNetwork::build(f, d, lakes, s);
    return TerrainWater::build(f, d, lakes, network);
}
void check(const TerrainWater::Surface& water, const TerrainPlayability::Result& r,
           const TerrainPlayability::Settings& s) {
    int q = r.resolution;
    const auto& ground = water.ground();
    std::vector<double> coordinates(q + 1);
    for (int x = 0; x <= q; ++x) coordinates[x] = ground.position(x, 0).x;
    auto coordinate = [&](int x) { return coordinates[x]; };
    auto centre = [&](uint32_t cell) {
        return glm::dvec2(float((coordinate(cell % q) + coordinate(cell % q + 1)) * .5),
                          float((coordinate(cell / q) + coordinate(cell / q + 1)) * .5));
    };
    // Exhaustive rectangle intersections independently verify the summed-area
    // expansion, including hazards touched only between route centre samples.
    for (int z = 0; z < q; ++z) for (int x = 0; x < q; ++x) {
        uint8_t flags = 0;
        double xmin = coordinate(x) - r.footprintRadius, xmax = coordinate(x + 1) + r.footprintRadius;
        double zmin = coordinate(z) - r.footprintRadius, zmax = coordinate(z + 1) + r.footprintRadius;
        if (xmin < -r.boundaryHalfExtent || zmin < -r.boundaryHalfExtent ||
            xmax > r.boundaryHalfExtent || zmax > r.boundaryHalfExtent) flags |= TerrainPlayability::Boundary;
        for (int zz = 0; zz < q; ++zz) for (int xx = 0; xx < q; ++xx)
            if (coordinate(xx) <= xmax && coordinate(xx + 1) >= xmin &&
                coordinate(zz) <= zmax && coordinate(zz + 1) >= zmin) flags |= r.terrainFlags[size_t(zz) * q + xx];
        require(flags == r.flags[size_t(z) * q + x], "footprint flags disagree with exhaustive geometry");
        require((r.component[size_t(z) * q + x] < 0) == bool(flags & TerrainPlayability::kRouteBlocked),
                "unsafe cell participates in route component");
    }
    if (!r.spawn) {
        require(r.status != TerrainPlayability::Status::Ready && r.route.empty(), "failure retained an accepted spawn/route");
        return;
    }
    require(r.status == TerrainPlayability::Status::Ready && r.route.size() >= 2 &&
            r.route.front() == r.spawn->cell && r.flags[r.spawn->cell] == 0, "invalid accepted spawn");
    require(r.components[r.spawn->component].area >= s.minimumConnectedArea && r.routeSpan >= s.minimumRouteSpan,
            "accepted map missed area/span requirement");
    require(r.spawn->position.y == ground.heightAt(r.spawn->position.x, r.spawn->position.z) &&
            !water.sampleAt(r.spawn->position.x, r.spawn->position.z), "spawn uses stale/wet ground");
    double length = 0;
    for (size_t i = 0; i < r.route.size(); ++i) {
        uint32_t cell = r.route[i];
        require(r.component[cell] == int32_t(r.spawn->component) && !(r.flags[cell] & TerrainPlayability::kRouteBlocked),
                "route leaves certified component");
        if (i) {
            uint32_t previous = r.route[i - 1];
            require(std::abs(int(cell % q) - int(previous % q)) + std::abs(int(cell / q) - int(previous / q)) == 1,
                    "route cuts a diagonal corner or jumps over a hazard");
            length += glm::length(centre(cell) - centre(previous));
        }
    }
    require(length == r.routeLength && glm::length(centre(r.route.back()) - centre(r.route.front())) == r.routeSpan,
            "route length/span is inconsistent");
    require(r.spawn->forward == glm::vec2(glm::normalize(centre(r.route[1]) - centre(r.route[0]))),
            "spawn does not face its first route step");
}
}

int main() {
    using namespace TerrainPlayability;
    Settings s; s.minimumConnectedArea = 100; s.minimumRouteSpan = 12;
    auto flat = water(field());
    auto ready = analyze(flat.surface, s);
    require(ready.status == Status::Ready && ready.components.size() == 1, "flat field is not playable");
    check(flat.surface, ready, s);
    auto impossible = s; impossible.minimumConnectedArea = 1e9;
    require(analyze(flat.surface, impossible).status == Status::InsufficientArea, "area failure not reported");
    impossible = s; impossible.minimumRouteSpan = 1e9;
    require(analyze(flat.surface, impossible).status == Status::InsufficientRouteSpan, "route-span failure not reported");
    auto slope = field();
    for (int z = 0; z < 41; ++z) for (int x = 0; x < 41; ++x) slope.heightmap.heights[z * 41 + x] = x * .5f;
    auto tilted = water(slope);
    require(analyze(tilted.surface, s).status == Status::NoDrySpawn, "steep spawn accepted on otherwise drivable terrain");
    impossible = s; impossible.maximumSlopeDegrees = 20;
    require(analyze(tilted.surface, impossible).status == Status::NoTraversableRegion, "excessive face slope accepted");

    // The gap is open at individual points, but too narrow for the whole
    // rotating hull. Widening it produces a continuous safe route component.
    for (int gap : {0, 5}) {
        std::vector<CollisionSystem::CircleObstacle> obstacles;
        for (int z = -20; z <= 20; ++z) if (std::abs(z) > gap) obstacles.push_back({{0, float(z)}, .45f});
        auto r = analyze(flat.surface, s, obstacles);
        require(r.components.size() == (gap == 0 ? 2 : 1), "obstacle gap ignored tank width or blocked a wide corridor");
        check(flat.surface, r, s);
    }
    // A single steep ridge splits a map even when centres on either side are
    // flat. No shading-normal averaging or point-only clearance may bridge it.
    auto ridge = field();
    for (int z = 0; z < 41; ++z) ridge.heightmap.heights[z * 41 + 20] = 10;
    auto ridged = water(ridge);
    auto separated = analyze(ridged.surface, s);
    require(separated.components.size() == 2, "thin steep ridge did not split driving space");
    check(ridged.surface, separated, s);

    // A tiny wet corner has a dry quad centre. Classifying only sampleAt at
    // the centre would miss the water and permit a hull to overlap it.
    auto pond = field(9);
    std::fill(pond.heightmap.heights.begin(), pond.heightmap.heights.end(), 4);
    for (int x = 0; x < 4; ++x) pond.heightmap.heights[4 * 9 + x] = 1;
    pond.heightmap.heights[4 * 9 + 4] = 0;
    auto wet = water(pond);
    require(!wet.surface.sampleAt(-.5f, -.5f) && wet.surface.triangleHasWater(2 * (3 * 8 + 3)),
            "tiny wet-corner fixture is missing");
    auto small = s; small.hullWidth = small.hullLength = .2f; small.clearance = 0;
    small.minimumConnectedArea = 1; small.minimumRouteSpan = 1;
    auto wr = analyze(wet.surface, small);
    require(wr.terrainFlags[3 * 8 + 3] & Water, "dry quad centre hid a wet corner");
    check(wet.surface, wr, small);

    auto fractional = field(18);
    fractional.playableWorldSize = fractional.heightmap.worldSize = 13.37f;
    fractional.spacing = fractional.playableWorldSize / 17;
    auto fractionalWater = water(fractional);
    auto fractionSettings = small; fractionSettings.boundaryInsetFraction = .073f;
    std::vector<CollisionSystem::CircleObstacle> smallObstacle{{{-.371f, .19f}, .127f}};
    auto fractionalResult = analyze(fractionalWater.surface, fractionSettings, smallObstacle);
    require(fractionalResult.status == Status::Ready, "fractional coordinate fixture has no route");
    check(fractionalWater.surface, fractionalResult, fractionSettings);

    for (int bad = 0; bad < 7; ++bad) {
        auto invalid = s;
        if (bad == 0) invalid.hullWidth = 0;
        if (bad == 1) invalid.clearance = -1;
        if (bad == 2) invalid.boundaryInsetFraction = .5f;
        if (bad == 3) invalid.maximumSlopeDegrees = 90;
        if (bad == 4) invalid.spawnSlopeDegrees = invalid.maximumSlopeDegrees + 1;
        if (bad == 5) invalid.minimumRouteSpan = std::numeric_limits<double>::infinity();
        if (bad == 6) invalid.minimumConnectedArea = 0;
        rejects([&] { analyze(flat.surface, invalid); });
    }
    std::vector<CollisionSystem::CircleObstacle> invalid{{{0, 0}, -1}};
    rejects([&] { analyze(flat.surface, s, invalid); });
    TerrainGenerator::Settings settings; settings.playability.emplace();
    rejects([&] { TerrainGenerator::build(settings); });
    settings.preset = TerrainGenerator::Preset::DrainedValley;
    settings.resolution = 33; settings.lakes.emplace(); settings.streams.emplace(); settings.channelCarving.emplace();
    settings.combinedWater = true; settings.erosion.duration = .25; settings.erosion.rainDuration = .15; settings.erosion.talusPasses = 1;
    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        settings.seed = seed; settings.erosion.workers = 1;
        auto a = TerrainGenerator::build(settings);
        require(a.playability.has_value(), "generator omitted playability analysis");
        auto direct = analyze(a.combinedWater->surface, *settings.playability);
        require(a.playability->flags == direct.flags && a.playability->route == direct.route, "generator analysed stale terrain/water");
        check(a.combinedWater->surface, *a.playability, *settings.playability);
        settings.erosion.workers = 4;
        auto b = TerrainGenerator::build(settings);
        require(a.playability->flags == b.playability->flags && a.playability->terrainFlags == b.playability->terrainFlags &&
                a.playability->component == b.playability->component && a.playability->route == b.playability->route &&
                a.playability->status == b.playability->status, "spawn/routes changed with worker count");
        auto without = settings; without.playability.reset();
        auto control = TerrainGenerator::build(without);
        require(control.surface.heightmap().heights == a.surface.heightmap().heights &&
                control.combinedWater->streamLevels == a.combinedWater->streamLevels &&
                control.combinedWater->surface.mesh().indices == a.combinedWater->surface.mesh().indices,
                "playability analysis changed ground/water");
    }
}
