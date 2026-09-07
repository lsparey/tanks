#include "scene/TerrainRuntime.h"
#include "scene/TerrainSelection.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "invalid runtime input accepted");
}
auto smallRecipe(uint32_t seed = 2654443100u) {
    auto s = TerrainRuntime::recipe(seed, 2.222f, 4.48f, MacroTerrain::Landform::Valley);
    s.resolution = 33; s.erosion.duration = .25; s.erosion.rainDuration = .15; s.erosion.talusPasses = 1;
    return s;
}
TerrainRuntime::State retained(TerrainGenerator::Settings s) {
    auto selected = TerrainSelection::select(s);
    require(selected.accepted.has_value(), "runtime fixture not accepted");
    auto& build = *selected.accepted;
    auto heights = build.surface.heightmap().heights;
    auto route = build.playability->route;
    auto waterIndices = build.combinedWater->surface.mesh().indices;
    auto state = TerrainRuntime::retain(build);
    require(state.ground.heightmap().heights == heights && state.water->ground().heightmap().heights == heights &&
            state.navigation->route == route && state.water->mesh().indices == waterIndices,
            "runtime dropped or changed final ground, water or navigation");
    require(!build.mesh.vertices.empty() && !build.mesh.indices.empty(), "retention consumed mesh before upload");
    for (size_t i = 0; i < build.mesh.vertices.size(); ++i)
        require(build.mesh.vertices[i].position.y == heights[i], "upload mesh differs from runtime ground");
    return state; // build and ALL generation scratch destroyed before queries
}
size_t stress(const TerrainRuntime::State& state, uint32_t seed) {
    require(state.reservation->protectedArea() >= state.playabilitySettings->minimumConnectedArea,
            "reservation shrank the minimum connected area");
    std::mt19937 rng(seed);
    float half = state.ground.heightmap().worldSize * .5f;
    std::uniform_real_distribution<float> coordinate(-half - 10, half + 10), radius(0, 9);
    std::vector<CollisionSystem::CircleObstacle> placed;
    for (int i = 0; i < 2000; ++i) {
        CollisionSystem::CircleObstacle o{{coordinate(rng), coordinate(rng)}, i % 11 ? radius(rng) : 0};
        if (state.reservation->allows(o)) placed.push_back(o);
    }
    require(placed.size() > 100, "reservation unnecessarily excludes the whole world");
    require(state.verifyObstacles(placed) >= state.playabilitySettings->minimumConnectedArea,
            "allowed obstacle circles destroyed the reserved area/route");
    const auto& spawn = *state.navigation->spawn;
    require(!state.reservation->allows({{spawn.position.x, spawn.position.z}, 0}), "zero-radius obstacle allowed at spawn");
    require(!state.allowsScenery({spawn.position.x, spawn.position.z}, 1), "scenery allowed at spawn");
    placed.push_back({{spawn.position.x, spawn.position.z}, .17f});
    rejects([&] { state.verifyObstacles(placed); });
    // Deliberately block the far end: verification must check the old route,
    // even if analysis could choose another valid route in the same region.
    auto cell = state.navigation->route.back(); int q = state.navigation->resolution;
    auto a = state.ground.position(cell % q, cell / q);
    auto b = state.ground.position(cell % q + 1, cell / q + 1);
    std::vector<CollisionSystem::CircleObstacle> endBlock{{{(a.x + b.x) * .5f, (a.z + b.z) * .5f}, .25f}};
    rejects([&] { state.verifyObstacles(endBlock); });

    // Every rendered water triangle has a wet interior; the same retained
    // surface must reject it for gameplay/placement after scratch destruction.
    const auto& mesh = state.water->mesh();
    size_t wet = 0;
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        auto at = (mesh.vertices[mesh.indices[i]].position + mesh.vertices[mesh.indices[i + 1]].position +
                   mesh.vertices[mesh.indices[i + 2]].position) / 3.0f;
        if (state.water->sampleAt(at.x, at.z)) {
            ++wet;
            require(!state.allowsScenery({at.x, at.z}, 0), "rendered water accepted as dry scenery ground");
        }
    }
    require(!state.allowsScenery({half - .1f, 0}, 1), "scenery crosses terrain edge");
    rejects([&] { state.allowsScenery({0, 0}, -1); });
    rejects([&] { state.allowsScenery({std::numeric_limits<float>::quiet_NaN(), 0}, 1); });
    return wet;
}
}

int main(int argc, char**) {
    auto s = smallRecipe();
    require(s.preset == TerrainGenerator::Preset::DrainedValley && s.combinedWater && s.channelCarving &&
            s.lakes && s.streams && s.playability && !s.streamSections,
            "runtime recipe omits a required stage or retains optional surveys");
    auto otherHull = TerrainRuntime::recipe(0, 3, 7);
    require(otherHull.macro.landform == MacroTerrain::Landform::Mixed, "runtime still forces a valley");
    require(otherHull.playability->hullWidth == 3 && otherHull.playability->hullLength == 7,
            "runtime ignored loaded hull dimensions");
    rejects([] { TerrainRuntime::recipe(0, 0, 4); });
    auto state = retained(s);
    require(stress(state, s.seed) > 0, "wet fixture lost its water coverage");
    auto trees = TerrainRuntime::placeTrees(state, s.seed, 6, 4);
    require(trees.size() == 100, "runtime did not preserve tree count");
    rejects([&] { TerrainRuntime::placeTrees(state, s.seed, 6, 180); });
    rejects([&] { TerrainRuntime::placeTrees(state, s.seed, 0, 1); });
    auto copied = state;
    auto moved = std::move(copied);
    require(moved.verifyObstacles({}) == state.verifyObstacles({}), "runtime copy/move lost owned navigation data");
    auto legacyBuild = TerrainGenerator::build({});
    auto legacy = TerrainRuntime::retain(legacyBuild);
    require(!legacy.water && !legacy.navigation && !legacy.reservation && legacy.allowsScenery({0, 0}, 1),
            "legacy runtime unexpectedly enabled valley policy");
    auto failed = TerrainGenerator::build(smallRecipe(7331));
    auto failedHeights = failed.surface.heightmap().heights;
    rejects([&] { TerrainRuntime::retain(failed); });
    require(failed.surface.heightmap().heights == failedHeights, "failed retention consumed ground");
    auto broken = TerrainGenerator::build(s);
    broken.playability->route.back() = 0;
    rejects([&] { TerrainRuntime::retain(broken); });

    // Optional production-resolution regression, kept out of fast unit tests.
    if (argc > 1) for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        auto full = retained(TerrainRuntime::recipe(seed, 2.222f, 4.48f));
        stress(full, seed);
        std::cout << "seed " << seed << ": route " << full.navigation->routeLength
                  << " m, reserved area " << full.reservation->protectedArea() << " m2; passed\n" << std::flush;
    }
}
