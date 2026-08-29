#pragma once

#include <glm/glm.hpp>

class CollisionSystem {
public:
    // Does the segment from `from` to `to` intersect the axis-aligned box
    // [boxMin, boxMax]? Tested as a swept segment rather than a point check
    // at the projectile's new position alone, so a fast-moving shell can't
    // tunnel through a box thinner than one frame's travel distance.
    // If it hits and outT is non-null, *outT (in [0,1]) gives the entry
    // point along the segment, for placing hit-feedback effects.
    static bool segmentIntersectsAABB(glm::vec3 from, glm::vec3 to, glm::vec3 boxMin,
                                       glm::vec3 boxMax, float* outT = nullptr);
};
