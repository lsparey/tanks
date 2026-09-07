#include "TerrainPlayability.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace TerrainPlayability {
const char* statusName(Status status) {
    switch (status) {
        case Status::Ready: return "ready";
        case Status::NoTraversableRegion: return "no-traversable-region";
        case Status::InsufficientArea: return "insufficient-area";
        case Status::NoDrySpawn: return "no-dry-spawn";
        case Status::InsufficientRouteSpan: return "insufficient-route-span";
    }
    throw std::invalid_argument("invalid playability status");
}
void validate(const Settings& s) {
    if (!std::isfinite(s.hullWidth) || s.hullWidth <= 0 || s.hullWidth > 100 ||
        !std::isfinite(s.hullLength) || s.hullLength <= 0 || s.hullLength > 100 ||
        !std::isfinite(s.clearance) || s.clearance < 0 || s.clearance > 100 ||
        !std::isfinite(s.boundaryInsetFraction) || s.boundaryInsetFraction < 0 || s.boundaryInsetFraction >= .5f ||
        !std::isfinite(s.maximumSlopeDegrees) || s.maximumSlopeDegrees < 0 || s.maximumSlopeDegrees >= 90 ||
        !std::isfinite(s.spawnSlopeDegrees) || s.spawnSlopeDegrees < 0 || s.spawnSlopeDegrees > s.maximumSlopeDegrees ||
        !std::isfinite(s.minimumConnectedArea) || s.minimumConnectedArea <= 0 ||
        !std::isfinite(s.minimumRouteSpan) || s.minimumRouteSpan <= 0)
        throw std::invalid_argument("invalid terrain playability settings");
}
size_t Result::payloadBytes() const {
    return (terrainFlags.capacity() + flags.capacity()) * sizeof(uint8_t) +
        component.capacity() * sizeof(int32_t) + components.capacity() * sizeof(Component) +
        route.capacity() * sizeof(uint32_t);
}

Result analyze(const TerrainWater::Surface& water, const Settings& s,
               std::span<const CollisionSystem::CircleObstacle> obstacles) {
    auto start = std::chrono::steady_clock::now();
    validate(s);
    const auto& ground = water.ground();
    int n = ground.heightmap().resolution, q = n - 1;
    if (n > 4097) throw std::invalid_argument("playability grid exceeds 4097 samples");
    for (const auto& o : obstacles)
        if (!std::isfinite(o.center.x) || !std::isfinite(o.center.y) || !std::isfinite(o.radius) || o.radius < 0)
            throw std::invalid_argument("invalid playability obstacle");
    size_t count = size_t(q) * q;
    Result r;
    r.resolution = q;
    r.footprintRadius = std::hypot(double(s.hullWidth), double(s.hullLength)) * .5 + s.clearance;
    // Round exactly like the current runtime boundary, then work in double.
    r.boundaryHalfExtent = ground.heightmap().worldSize * (.5f - s.boundaryInsetFraction);
    r.terrainFlags.resize(count); r.flags.resize(count); r.component.assign(count, -1);
    std::vector<double> coordinates(n), centres(q);
    for (int x = 0; x < n; ++x) {
        coordinates[x] = ground.position(x, 0).x;
        if (x && coordinates[x] <= coordinates[x - 1]) throw std::invalid_argument("unrepresentable playability grid");
        if (x) centres[x - 1] = float((coordinates[x - 1] + coordinates[x]) * .5);
    }
    auto centre = [&](uint32_t cell) { return glm::dvec2(centres[cell % q], centres[cell / q]); };
    double slopeLimit = std::tan(s.maximumSlopeDegrees * std::numbers::pi / 180);
    double spawnLimit = std::tan(s.spawnSlopeDegrees * std::numbers::pi / 180);
    for (int z = 0; z < q; ++z) for (int x = 0; x < q; ++x) {
        uint32_t cell = uint32_t(z) * q + x;
        auto indices = TerrainSurface::quadIndices(n, x, z);
        for (int t = 0; t < 2; ++t) {
            if (water.triangleHasWater(2 * cell + t)) r.terrainFlags[cell] |= Water;
            auto position = [&](uint32_t i) { return glm::dvec3(ground.position(i % n, i / n)); };
            auto a = position(indices[t * 3]);
            auto normal = glm::cross(position(indices[t * 3 + 1]) - a, position(indices[t * 3 + 2]) - a);
            double slope = std::hypot(normal.x, normal.z) / normal.y;
            if (slope > slopeLimit) r.terrainFlags[cell] |= Steep;
            if (slope > spawnLimit) r.terrainFlags[cell] |= SpawnSteep;
        }
    }
    // Closed intersection deliberately includes cells merely touching a
    // hazard. Rounding or a tiny wet corner cannot open a false-safe passage.
    auto range = [&](double minimum, double maximum) {
        int first = std::clamp(int(std::lower_bound(coordinates.begin(), coordinates.end(), minimum) - coordinates.begin()) - 1, 0, q);
        int end = std::clamp(int(std::upper_bound(coordinates.begin(), coordinates.end(), maximum) - coordinates.begin()), 0, q);
        return std::pair{first, end};
    };
    for (const auto& o : obstacles) {
        auto [xmin, xmax] = range(double(o.center.x) - o.radius, double(o.center.x) + o.radius);
        auto [zmin, zmax] = range(double(o.center.y) - o.radius, double(o.center.y) + o.radius);
        for (int z = zmin; z < zmax; ++z) for (int x = xmin; x < xmax; ++x) {
            auto closest = glm::clamp(glm::dvec2(o.center), glm::dvec2(coordinates[x], coordinates[z]),
                                     glm::dvec2(coordinates[x + 1], coordinates[z + 1]));
            auto delta = closest - glm::dvec2(o.center);
            if (glm::dot(delta, delta) <= double(o.radius) * o.radius) r.terrainFlags[size_t(z) * q + x] |= Obstacle;
        }
    }
    // Four summed-area tables give O(1) exact hazard-presence checks over
    // each expanded cell rectangle, independent of tank size/grid spacing.
    std::vector<std::array<uint32_t, 4>> prefix(size_t(n) * n);
    for (int z = 0; z < q; ++z) for (int x = 0; x < q; ++x)
        for (int bit = 0; bit < 4; ++bit)
            prefix[size_t(z + 1) * n + x + 1][bit] = ((r.terrainFlags[size_t(z) * q + x] >> bit) & 1) +
                prefix[size_t(z) * n + x + 1][bit] + prefix[size_t(z + 1) * n + x][bit] - prefix[size_t(z) * n + x][bit];
    for (int z = 0; z < q; ++z) for (int x = 0; x < q; ++x) {
        auto& flags = r.flags[size_t(z) * q + x];
        double xmin = coordinates[x] - r.footprintRadius, xmax = coordinates[x + 1] + r.footprintRadius;
        double zmin = coordinates[z] - r.footprintRadius, zmax = coordinates[z + 1] + r.footprintRadius;
        if (xmin < -r.boundaryHalfExtent || zmin < -r.boundaryHalfExtent ||
            xmax > r.boundaryHalfExtent || zmax > r.boundaryHalfExtent) flags |= Boundary;
        auto [x0, x1] = range(xmin, xmax); auto [z0, z1] = range(zmin, zmax);
        for (int bit = 0; bit < 4; ++bit)
            if (prefix[size_t(z1) * n + x1][bit] + prefix[size_t(z0) * n + x0][bit] >
                prefix[size_t(z0) * n + x1][bit] + prefix[size_t(z1) * n + x0][bit]) flags |= uint8_t(1 << bit);
    }
    auto neighbours = [&](uint32_t cell, auto visit) {
        int x = cell % q, z = cell / q;
        if (x > 0) visit(cell - 1);
        if (x + 1 < q) visit(cell + 1);
        if (z > 0) visit(cell - q);
        if (z + 1 < q) visit(cell + q);
    };
    std::vector<uint32_t> queue;
    std::vector<uint32_t> candidates;
    for (uint32_t first = 0; first < count; ++first) {
        if (r.component[first] >= 0 || (r.flags[first] & kRouteBlocked)) continue;
        int32_t id = int32_t(r.components.size());
        Component component{first};
        glm::dvec2 centroid(0);
        queue.clear(); queue.push_back(first); r.component[first] = id;
        for (size_t k = 0; k < queue.size(); ++k) {
            uint32_t cell = queue[k]; int x = cell % q, z = cell / q;
            double area = (coordinates[x + 1] - coordinates[x]) * (coordinates[z + 1] - coordinates[z]);
            component.area += area; centroid += centre(cell) * area;
            neighbours(cell, [&](uint32_t next) {
                if (r.component[next] < 0 && !(r.flags[next] & kRouteBlocked)) {
                    r.component[next] = id; queue.push_back(next);
                }
            });
        }
        component.cells = uint32_t(queue.size());
        centroid /= component.area;
        uint32_t candidate = uint32_t(count);
        double best = std::numeric_limits<double>::infinity();
        for (uint32_t cell : queue) if (!r.flags[cell]) {
            auto delta = centre(cell) - centroid;
            double distance = glm::dot(delta, delta);
            if (distance < best || (distance == best && cell < candidate)) { best = distance; candidate = cell; }
        }
        r.components.push_back(component); candidates.push_back(candidate);
    }
    std::vector<uint32_t> order(r.components.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return r.components[a].area != r.components[b].area ? r.components[a].area > r.components[b].area : a < b;
    });
    r.status = r.components.empty() ? Status::NoTraversableRegion : Status::InsufficientArea;
    std::vector<int32_t> parent(count, -1);
    for (uint32_t id : order) {
        if (r.components[id].area < s.minimumConnectedArea) break;
        if (r.status == Status::InsufficientArea) r.status = Status::NoDrySpawn;
        uint32_t candidate = candidates[id];
        if (candidate == count) continue;
        r.status = Status::InsufficientRouteSpan;
        // One deterministic interior spawn candidate per component. Failure is
        // conservative, not proof that every possible spawn/layout is invalid.
        queue.clear(); queue.push_back(candidate); parent[candidate] = int32_t(candidate);
        uint32_t goal = candidate;
        double farthest = 0;
        for (size_t k = 0; k < queue.size(); ++k) {
            uint32_t cell = queue[k];
            auto delta = centre(cell) - centre(candidate);
            double distance = glm::dot(delta, delta);
            if (distance > farthest || (distance == farthest && cell < goal)) { farthest = distance; goal = cell; }
            neighbours(cell, [&](uint32_t next) {
                if (r.component[next] == int32_t(id) && parent[next] < 0) {
                    parent[next] = int32_t(cell); queue.push_back(next);
                }
            });
        }
        if (std::sqrt(farthest) < s.minimumRouteSpan) continue;
        for (uint32_t cell = goal;; cell = uint32_t(parent[cell])) {
            r.route.push_back(cell);
            if (cell == candidate) break;
        }
        std::reverse(r.route.begin(), r.route.end());
        for (size_t k = 1; k < r.route.size(); ++k) r.routeLength += glm::length(centre(r.route[k]) - centre(r.route[k - 1]));
        r.routeSpan = std::sqrt(farthest);
        auto at = centre(candidate);
        r.spawn = Spawn{candidate, id, {float(at.x), ground.heightAt(float(at.x), float(at.y)), float(at.y)},
                        glm::vec2(glm::normalize(centre(r.route[1]) - at))};
        r.status = Status::Ready;
        break;
    }
    r.workingBytes = prefix.capacity() * sizeof(std::array<uint32_t, 4>) +
        (coordinates.capacity() + centres.capacity()) * sizeof(double) + parent.capacity() * sizeof(int32_t) +
        (queue.capacity() + candidates.capacity() + order.capacity()) * sizeof(uint32_t);
    r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return r;
}
} // namespace TerrainPlayability
