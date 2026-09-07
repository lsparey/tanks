#pragma once

#include "scene/ShorelineIndex.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>

// Original exhaustive query, independent of the BVH's partition and bounds.
inline std::optional<float> shorelineReference(std::span<const ShorelineIndex::Segment> shores, float x, float z) {
    if (shores.empty()) return std::nullopt;
    double best = std::numeric_limits<double>::infinity();
    glm::dvec2 p(x, z);
    for (const auto& shore : shores) {
        glm::dvec2 a(shore.a), edge = glm::dvec2(shore.b) - a;
        double length = glm::dot(edge, edge);
        double t = length > 0 ? std::clamp(glm::dot(p - a, edge) / length, 0.0, 1.0) : 0;
        auto delta = p - (a + t * edge);
        best = std::min(best, glm::dot(delta, delta));
    }
    return float(std::min(std::sqrt(best), double(std::numeric_limits<float>::max())));
}

template<class Surface> void checkShorelineQueries(const Surface& surface, float worldSize) {
    // Includes the playable boundary and apron/outside points. Sign belongs
    // to the surface's exact wet classification, not to the spatial index.
    for (int z = -12; z <= 12; ++z) for (int x = -12; x <= 12; ++x) {
        float px = x * worldSize / 20, pz = z * worldSize / 20;
        auto expected = shorelineReference(surface.shores(), px, pz);
        if (expected && surface.sampleAt(px, pz)) *expected = -*expected;
        auto actual = surface.shorelineDistanceAt(px, pz);
        if (actual != expected || (actual && std::signbit(*actual) != std::signbit(*expected)))
            throw std::runtime_error("indexed surface shoreline query differs from exhaustive scan");
    }
}
