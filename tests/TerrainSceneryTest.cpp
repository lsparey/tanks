#include "render/Mesh.h"
#include "scene/TreeGenerator.h"
#include "scene/TerrainRuntime.h"
#include "scene/TerrainSelection.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void check(uint32_t seed, float radius, bool production) {
    auto recipe = TerrainRuntime::recipe(seed, 2.222f, 4.48f);
    if (!production) {
        recipe.macro.landform = MacroTerrain::Landform::Valley; // original wet fixture
        recipe.resolution = 33; recipe.erosion.duration = .25; recipe.erosion.rainDuration = .15; recipe.erosion.talusPasses = 1;
    }
    auto selected = TerrainSelection::select(recipe);
    require(selected.accepted.has_value(), "scenery fixture not accepted");
    auto state = TerrainRuntime::retain(*selected.accepted);
    auto trees = TerrainRuntime::placeTrees(state, seed, 6, radius);
    auto replay = TerrainRuntime::placeTrees(state, seed, 6, radius);
    require(trees.size() == 100 && replay.size() == trees.size(), "scenery reduced tree count");
    std::vector<CollisionSystem::CircleObstacle> obstacles;
    const auto& spawn = state.navigation->spawn->position;
    for (size_t i = 0; i < trees.size(); ++i) {
        const auto& tree = trees[i];
        require(tree.position == replay[i].position && tree.yaw == replay[i].yaw &&
                tree.scale == replay[i].scale && tree.meshVariant == replay[i].meshVariant, "tree replay changed");
        glm::vec2 at(tree.position.x, tree.position.z);
        require(tree.scale >= .8f && tree.scale <= 1.4f && tree.meshVariant >= 0 && tree.meshVariant < 6,
                "tree scale/variant distribution changed");
        require(state.allowsScenery(at, radius * tree.scale) &&
                glm::length(at - glm::vec2(spawn.x, spawn.z)) >= 8 &&
                tree.position.y == state.ground.heightAt(at.x, at.y), "tree intersects water/route/spawn or floats");
        for (size_t j = 0; j < i; ++j)
            require(glm::length(at - glm::vec2(trees[j].position.x, trees[j].position.z)) >= 3, "tree spacing regressed");
        obstacles.push_back({at, .17f * tree.scale});
    }
    state.verifyObstacles(obstacles);
    bool exhausted = false;
    try { TerrainRuntime::placeTrees(state, seed, 6, state.ground.heightmap().worldSize); }
    catch (const std::runtime_error&) { exhausted = true; }
    require(exhausted, "tree placement forced an illegal candidate on exhaustion");
    std::cout << "seed " << seed << ": 100 trees, bark bound " << radius << " m, route verified\n" << std::flush;
}
}

int main(int argc, char**) {
    // Same six species/seeds/scales as Application, using actual CPU bark
    // vertices. No graphics device/window is created by this test.
    double radius = .17;
    for (int i = 0; i < 6; ++i) {
        auto tree = TreeGenerator::generate(i + 1, static_cast<TreeGenerator::Species>(i / 2), i % 2 == 0 ? .88f : 1.f);
        auto geometry = Mesh::treeBarkGeometry(glm::vec3(1), tree, 0);
        for (const auto& vertex : geometry.vertices)
            radius = std::max(radius, std::hypot(double(vertex.position.x), double(vertex.position.z)));
    }
    float bound = std::nextafter(float(radius), std::numeric_limits<float>::infinity());
    check(2654443100u, bound, false);
    if (argc > 1) for (uint32_t seed : TerrainGenerator::kRegressionSeeds) check(seed, bound, true);
}
