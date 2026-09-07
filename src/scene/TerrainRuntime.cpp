#include "TerrainRuntime.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <utility>

namespace TerrainRuntime {
namespace {
auto range(const std::vector<double>& coordinates, double low, double high) {
    int q = int(coordinates.size()) - 1;
    return std::pair{
        std::clamp(int(std::lower_bound(coordinates.begin(), coordinates.end(), low) - coordinates.begin()) - 1, 0, q),
        std::clamp(int(std::upper_bound(coordinates.begin(), coordinates.end(), high) - coordinates.begin()), 0, q)};
}
}

TerrainGenerator::Settings recipe(uint32_t seed, float hullWidth, float hullLength, MacroTerrain::Landform landform) {
    TerrainGenerator::Settings s;
    s.preset = TerrainGenerator::Preset::DrainedValley;
    s.seed = seed; s.resolution = 257; s.erosion.workers = 4;
    MacroTerrain::landformName(landform); // fail invalid input before generation
    s.macro.landform = landform;
    s.lakes.emplace(); s.streams.emplace(); s.channelCarving.emplace();
    s.combinedWater = true; s.playability.emplace();
    s.playability->hullWidth = hullWidth; s.playability->hullLength = hullLength;
    TerrainPlayability::validate(*s.playability);
    return s;
}

Reservation::Reservation(const TerrainSurface& ground, const TerrainPlayability::Result& nav, double minimumArea) {
    int q = ground.heightmap().resolution - 1;
    size_t count = size_t(q) * q;
    if (nav.status != TerrainPlayability::Status::Ready || !nav.spawn || nav.route.size() < 2 ||
        nav.resolution != q || nav.component.size() != count || nav.flags.size() != count ||
        nav.spawn->cell >= count || nav.route.front() != nav.spawn->cell ||
        !std::isfinite(minimumArea) || minimumArea <= 0 ||
        !std::isfinite(nav.footprintRadius) || nav.footprintRadius <= 0)
        throw std::invalid_argument("reservation requires an accepted terrain route");
    coordinates_.resize(q + 1); reserved_.resize(count);
    for (int x = 0; x <= q; ++x) coordinates_[x] = ground.position(x, 0).x;
    std::vector<uint8_t> selected(count);
    std::vector<uint32_t> queue;
    auto add = [&](uint32_t cell) {
        if (cell >= count || nav.component[cell] != int32_t(nav.spawn->component) ||
            (nav.flags[cell] & TerrainPlayability::kRouteBlocked))
            throw std::invalid_argument("reservation route leaves its safe component");
        if (selected[cell]) return;
        selected[cell] = 1; queue.push_back(cell);
        int x = cell % q, z = cell / q;
        protectedArea_ += (coordinates_[x + 1] - coordinates_[x]) * (coordinates_[z + 1] - coordinates_[z]);
    };
    for (size_t i = 0; i < nav.route.size(); ++i) {
        uint32_t cell = nav.route[i];
        if (i && std::abs(int(cell % q) - int(nav.route[i - 1] % q)) +
                 std::abs(int(cell / q) - int(nav.route[i - 1] / q)) != 1)
            throw std::invalid_argument("reservation route is not contiguous");
        add(cell);
    }
    // Multi-source BFS grows from the already connected route; every added
    // cell stays connected, including when the required area is reached early.
    for (size_t k = 0; k < queue.size() && protectedArea_ < minimumArea; ++k) {
        uint32_t cell = queue[k]; int x = cell % q, z = cell / q;
        auto visit = [&](uint32_t next) {
            if (nav.component[next] == int32_t(nav.spawn->component)) add(next);
        };
        if (x > 0) visit(cell - 1);
        if (x + 1 < q) visit(cell + 1);
        if (z > 0) visit(cell - q);
        if (z + 1 < q) visit(cell + q);
    }
    if (protectedArea_ < minimumArea) throw std::invalid_argument("reservation has insufficient connected area");
    for (uint32_t cell : queue) {
        int x = cell % q, z = cell / q;
        auto [x0, x1] = range(coordinates_, coordinates_[x] - nav.footprintRadius, coordinates_[x + 1] + nav.footprintRadius);
        auto [z0, z1] = range(coordinates_, coordinates_[z] - nav.footprintRadius, coordinates_[z + 1] + nav.footprintRadius);
        for (int zz = z0; zz < z1; ++zz) for (int xx = x0; xx < x1; ++xx) reserved_[size_t(zz) * q + xx] = 1;
    }
}

bool Reservation::allows(const CollisionSystem::CircleObstacle& obstacle) const {
    const auto& c = obstacle.center;
    double radius = obstacle.radius;
    if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(radius) || radius < 0)
        throw std::invalid_argument("invalid scenery footprint");
    int q = int(coordinates_.size()) - 1;
    auto [x0, x1] = range(coordinates_, double(c.x) - radius, double(c.x) + radius);
    auto [z0, z1] = range(coordinates_, double(c.y) - radius, double(c.y) + radius);
    for (int z = z0; z < z1; ++z) for (int x = x0; x < x1; ++x) {
        if (!reserved_[size_t(z) * q + x]) continue;
        auto closest = glm::clamp(glm::dvec2(c), glm::dvec2(coordinates_[x], coordinates_[z]),
                                 glm::dvec2(coordinates_[x + 1], coordinates_[z + 1]));
        auto delta = closest - glm::dvec2(c);
        if (glm::dot(delta, delta) <= radius * radius) return false;
    }
    return true;
}

State retain(TerrainGenerator::BuildResult& build) {
    bool legacy = build.settings.preset == TerrainGenerator::Preset::Legacy;
    if (!legacy && (build.settings.preset != TerrainGenerator::Preset::DrainedValley ||
        !build.combinedWater || !build.playability || !build.settings.playability ||
        build.playability->status != TerrainPlayability::Status::Ready || !build.playability->spawn))
        throw std::invalid_argument("runtime requires accepted drained-valley terrain");
    // Validate/build the reservation before moving any payload out of build.
    std::optional<Reservation> reservation;
    if (!legacy) reservation.emplace(build.surface, *build.playability, build.settings.playability->minimumConnectedArea);
    State state{std::move(build.surface), {}, {}, std::move(reservation), {}};
    if (!legacy) {
        state.water.emplace(std::move(build.combinedWater->surface));
        state.navigation = std::move(build.playability);
        state.playabilitySettings = build.settings.playability;
    }
    return state;
}

bool State::allowsScenery(glm::vec2 center, float radius) const {
    if (!reservation) return true;
    if (!reservation->allows({center, radius})) return false;
    float half = ground.heightmap().worldSize * .5f;
    if (std::abs(center.x) + radius >= half || std::abs(center.y) + radius >= half ||
        water->sampleAt(center.x, center.y)) return false;
    // Full-apron contours catch wet footprints even when their centres are dry.
    auto shore = water->shorelineDistanceAt(center.x, center.y);
    return !shore || *shore > radius;
}

double State::verifyObstacles(std::span<const CollisionSystem::CircleObstacle> obstacles) const {
    if (!water || !navigation || !playabilitySettings) throw std::logic_error("no runtime route to verify");
    auto final = TerrainPlayability::analyze(*water, *playabilitySettings, obstacles);
    const auto& spawn = *navigation->spawn;
    int32_t component = final.component.at(spawn.cell);
    if (final.flags.at(spawn.cell) || component < 0 ||
        final.components.at(component).area < playabilitySettings->minimumConnectedArea)
        throw std::runtime_error("scenery invalidated the selected terrain spawn or connected area");
    for (uint32_t cell : navigation->route)
        if ((final.flags.at(cell) & TerrainPlayability::kRouteBlocked) || final.component.at(cell) != component)
            throw std::runtime_error("scenery blocked the selected terrain route");
    return final.components.at(component).area;
}

std::vector<TreeInstance> placeTrees(const State& state, uint32_t seed, int variants, float barkRadius) {
    if (!state.reservation || !state.navigation || variants < 1 || !std::isfinite(barkRadius) || barkRadius < .17f)
        throw std::invalid_argument("tree placement requires accepted terrain and valid mesh bounds");
    const auto& spawn = state.navigation->spawn->position;
    glm::vec2 spawnXZ(spawn.x, spawn.z);
    float half = state.ground.heightmap().worldSize * .5f - 3;
    float radius = barkRadius * 1.4f;
    std::mt19937 rng(seed ^ 0x302u);
    std::uniform_real_distribution<float> coordDist(-half, half), yawDist(0, 6.2831853f), scaleDist(.8f, 1.4f);
    std::uniform_int_distribution<int> variantDist(0, variants - 1);
    std::vector<TreeInstance> trees;
    for (int i = 0; i < 100; ++i) {
        bool found = false;
        glm::vec2 pos(0);
        for (int attempt = 0; attempt < 256; ++attempt) {
            glm::vec2 candidate(coordDist(rng), coordDist(rng));
            if (glm::length(candidate - spawnXZ) < 8 || !state.allowsScenery(candidate, radius)) continue;
            if (std::any_of(trees.begin(), trees.end(), [&](const auto& tree) {
                    return glm::length(glm::vec2(tree.position.x, tree.position.z) - candidate) < 3;
                })) continue;
            pos = candidate; found = true; break;
        }
        if (!found) throw std::runtime_error("cannot place all 100 trees safely on selected terrain");
        TreeInstance tree;
        tree.position = {pos.x, state.ground.heightAt(pos.x, pos.y), pos.y};
        tree.yaw = yawDist(rng); tree.scale = scaleDist(rng); tree.meshVariant = variantDist(rng);
        trees.push_back(std::move(tree));
    }
    return trees;
}
} // namespace TerrainRuntime
