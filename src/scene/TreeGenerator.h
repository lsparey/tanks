#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace TreeGenerator {
enum class Species { Pine, Ash, Oak };
struct Branch {
    glm::vec3 base, tip;
    float baseRadius, tipRadius;
};
struct Spray {
    glm::vec3 center;
    glm::mat3 axes; // long axis, transverse axis, thickness axis
    glm::vec3 radii;
    uint32_t seed;
    uint32_t group = 0; // owning bough, shared by every foliage LOD
};
struct Tree {
    std::vector<Branch> branches;
    std::vector<Spray> sprays;
    Species species = Species::Pine;
};
// Generate the skeleton and leaf layout once. All materials, raster LODs
// and shadow proxies consume this same description. Density only selects
// sprays; it never changes branch structure or moves the surviving leaves.
Tree generate(uint32_t seed, Species species, float density);
} // namespace TreeGenerator
