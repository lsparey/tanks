#include "scene/TreeGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool finite(glm::vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
std::array<float, 3> pointKey(glm::vec3 p) { return {p.x, p.y, p.z}; }
}
int main() {
    using namespace TreeGenerator;
    for (Species species : {Species::Pine, Species::Ash, Species::Oak}) {
        for (uint32_t seed = 1; seed <= 24; ++seed) {
            Tree sparse = generate(seed, species, .45f);
            Tree full = generate(seed, species, 1.0f);
            Tree repeat = generate(seed, species, 1.0f);
            Tree bare = generate(seed, species, 0.0f);
            require(!sparse.sprays.empty() && sparse.sprays.size() < full.sprays.size(), "Density must change leaf coverage");
            require(bare.sprays.empty(), "Zero density must leave bare branches");
            require(sparse.branches.size() == full.branches.size() && bare.branches.size() == full.branches.size(), "Density changed the skeleton");
            require(repeat.sprays.size() == full.sprays.size(), "Non-deterministic foliage");
            std::set<std::array<float, 3>> twigTips;
            std::set<std::tuple<std::array<float, 3>, std::array<float, 3>, uint32_t>> fullSprays;
            for (size_t i = 0; i < full.branches.size(); ++i) {
                const Branch& b = full.branches[i];
                require(finite(b.base) && finite(b.tip) && glm::length(b.tip-b.base) > .001f, "Invalid branch");
                require(b.baseRadius >= b.tipRadius && b.tipRadius > 0, "Invalid taper");
                require(b.base == sparse.branches[i].base && b.tip == sparse.branches[i].tip, "Density moved a branch");
                require(b.base == bare.branches[i].base && b.tip == bare.branches[i].tip, "Bare tree moved a branch");
                require(glm::length(b.tip-glm::vec3(0,2.4f,0)) + b.baseRadius < 3.5f, "Branch exceeds scene culling bound");
                twigTips.insert(pointKey(b.tip));
            }
            for (size_t i = 0; i < full.sprays.size(); ++i) {
                const Spray& s = full.sprays[i];
                require(s.center == repeat.sprays[i].center && s.seed == repeat.sprays[i].seed, "Non-deterministic spray");
                require(finite(s.center) && finite(s.radii) && glm::all(glm::greaterThan(s.radii,glm::vec3(0))), "Invalid spray");
                require(s.radii.z < s.radii.x * .4f, "Foliage inflated into a ball");
                for(int axis=0;axis<3;++axis) {
                    require(std::abs(glm::length(s.axes[axis])-1.f)<1e-5f, "Non-unit spray axis");
                    require(std::abs(glm::dot(s.axes[axis],s.axes[(axis+1)%3]))<1e-5f, "Non-orthogonal spray axes");
                }
                require(glm::determinant(s.axes)>0.99f, "Mirrored spray winding");
                require(glm::length(s.center-glm::vec3(0,2.4f,0)) + glm::length(s.radii)*1.08f < 3.5f, "Spray exceeds scene culling bound");
                require(twigTips.contains(pointKey(s.center)), "Floating spray without a supporting twig");
                fullSprays.emplace(pointKey(s.center), pointKey(s.radii), s.seed);
            }
            for (const Spray& s : sparse.sprays) {
                require(fullSprays.contains({pointKey(s.center), pointKey(s.radii), s.seed}), "Thinning moved surviving leaves");
            }
        }
    }
    auto pine = generate(7,Species::Pine,1);
    auto ash = generate(7,Species::Ash,1);
    auto oak = generate(7,Species::Oak,1);
    require(pine.branches.front().tip.y > 4 && ash.branches.front().tip.y > 3.5f && oak.branches.front().tip.y < 1.5f, "Species need distinct trunk/fork structures");
}
