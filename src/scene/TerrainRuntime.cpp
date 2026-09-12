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

TerrainGenerator::Settings recipe(uint32_t seed, float hullWidth, float hullLength, MacroTerrain::Landform landform, int resolution, int refinementPasses) {
    if (resolution != 257 && resolution != 513)
        throw std::invalid_argument("runtime terrain resolution must be 257 or 513");
    if (refinementPasses < 0 || refinementPasses > 2)
        throw std::invalid_argument("runtime terrain refinement requires 0, 1 or 2 passes");
    if (refinementPasses && resolution != 257)
        throw std::invalid_argument("runtime refinement requires the 257 erosion grid");
    TerrainGenerator::Settings s;
    s.preset = TerrainGenerator::Preset::DrainedValley;
    s.refinementPasses = refinementPasses;
    s.seed = seed; s.resolution = resolution; s.erosion.workers = 4;
    // Game recipe uses a 18-second storm instead of the solver's 24-second
    // default: the 16-seed sweep stays fully clean (zero dry sections/spill
    // controls, all playable), height/water previews are near-identical --
    // channel readability comes mostly from the duration-independent carving
    // pass -- and median generation drops from 3.3 s to 2.3 s. The probe and
    // fixtures keep the 24-second default for recorded-baseline continuity.
    s.erosion.duration = 18; s.erosion.rainDuration = 13.5;
    MacroTerrain::landformName(landform); // fail invalid input before generation
    s.macro.landform = landform;
    s.lakes.emplace(); s.streams.emplace(); s.channelCarving.emplace();
    s.combinedWater = true; s.materials.emplace(); s.playability.emplace();
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
    State state{std::move(build.surface), {}, {}, {}, std::move(reservation), {}};
    if (!legacy) {
        state.water.emplace(std::move(build.combinedWater->surface));
        state.navigation = std::move(build.playability);
        state.materials = std::move(build.materials);
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
    std::uniform_real_distribution<float> coordDist(-half, half), yawDist(0, 6.2831853f), unitDist(0, 1);
    std::uniform_int_distribution<int> variantDist(0, variants - 1);

    auto steepness = [&](glm::vec2 at) { return 1.f - state.ground.contactNormalAt(at.x, at.y).y; };
    // Soil appeal weights the survivors: flat ground beats a hillside, and
    // moist ground inside the shoreline apron (but clear of the water) most of
    // all. Generated material fields, when present, favour damp soil and
    // reject exposed rock with the same data the ground shading uses.
    auto appeal = [&](glm::vec2 at) {
        float score = 1.f - steepness(at) * 4.f;
        auto shore = state.water->shorelineDistanceAt(at.x, at.y);
        if (shore && *shore > radius) score += .5f;
        if (state.materials)
            score += state.materials->moistureAt(at.x, at.y) * .4f - state.materials->rockAt(at.x, at.y) * .8f;
        return score;
    };

    // Thomas-style cluster process: tight coppices sharing a dominant species
    // and stature, sized so members pack near the spacing floor, with a few
    // open-grown loners in the clearings between. Area-weighted picking keeps
    // density constant, so large groves read as woods and small ones as copses.
    struct Grove { glm::vec2 center; float radius, statureBias; int variant; };
    std::vector<Grove> groves;
    float totalWeight = 0;
    for (int g = 0; g < 7; ++g) {
        glm::vec2 center(coordDist(rng), coordDist(rng));
        for (int attempt = 0; attempt < 24; ++attempt) {
            if (steepness(center) < .1f && state.allowsScenery(center, radius)) break;
            center = {coordDist(rng), coordDist(rng)};
        }
        groves.push_back({center, 3.5f + 5 * unitDist(rng), .2f * unitDist(rng) - .1f, variantDist(rng)});
        totalWeight += groves.back().radius * groves.back().radius;
    }
    auto pickGrove = [&]() -> const Grove* {
        float pick = unitDist(rng) * totalWeight;
        for (const auto& grove : groves)
            if ((pick -= grove.radius * grove.radius) <= 0) return &grove;
        return &groves.back();
    };

    std::vector<TreeInstance> trees;
    for (int i = 0; i < 100; ++i) {
        // Membership is decided per tree, not per attempt: a full grove spills
        // outward around its edge instead of defecting into the clearings.
        const Grove* home = unitDist(rng) < .88f ? pickGrove() : nullptr;
        bool found = false;
        glm::vec2 pos(0);
        for (int attempt = 0; attempt < 256; ++attempt) {
            // Late attempts go blind uniform so unusable groves cannot make an
            // otherwise placeable terrain exhaust.
            bool blind = attempt >= 192;
            glm::vec2 candidate;
            if (home && !blind) {
                float spread = home->radius * (1 + attempt * .03f);
                float away = spread * std::pow(unitDist(rng), .7f), angle = yawDist(rng);
                candidate = home->center + away * glm::vec2(std::cos(angle), std::sin(angle));
                if (std::abs(candidate.x) > half || std::abs(candidate.y) > half) continue;
            } else {
                candidate = {coordDist(rng), coordDist(rng)};
            }
            if (steepness(candidate) > .19f) continue; // past ~37 degrees nothing roots
            if (glm::length(candidate - spawnXZ) < 8 || !state.allowsScenery(candidate, radius)) continue;
            if (std::any_of(trees.begin(), trees.end(), [&](const auto& tree) {
                    return glm::length(glm::vec2(tree.position.x, tree.position.z) - candidate) < 3;
                })) continue;
            if (!blind && unitDist(rng) > appeal(candidate)) continue;
            if (blind) home = nullptr; // rescued uniformly: style it as a loner
            pos = candidate; found = true; break;
        }
        if (!found) throw std::runtime_error("cannot place all 100 trees safely on selected terrain");
        TreeInstance tree;
        tree.position = {pos.x, state.ground.heightAt(pos.x, pos.y), pos.y};
        tree.yaw = yawDist(rng);
        tree.scale = home ? 1.1f + home->statureBias + .4f * unitDist(rng) - .2f : 1.f + .4f * unitDist(rng);
        tree.meshVariant = home && unitDist(rng) < .7f ? home->variant : variantDist(rng);
        trees.push_back(std::move(tree));
    }
    return trees;
}
} // namespace TerrainRuntime
