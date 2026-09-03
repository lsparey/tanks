#pragma once

#include <vector>

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

    // Same swept-segment idea as segmentIntersectsAABB, for a sphere instead
    // of a box -- used for shell-vs-tree/rock hits (see
    // Application::updateProjectilesAndCollisions), where a real silhouette
    // isn't worth the complexity and a sphere is a closer approximation
    // than an axis-aligned box would be for a roughly-round canopy/boulder.
    static bool segmentIntersectsSphere(glm::vec3 from, glm::vec3 to, glm::vec3 center, float radius,
                                         float* outT = nullptr);

    // A simple round, static obstacle in the XZ plane -- used to approximate
    // trees/rocks for tank collision without needing their real (irregular)
    // silhouettes.
    struct CircleObstacle {
        glm::vec2 center;
        float radius;
    };

    struct CircleCollisionResult {
        glm::vec2 position;
        glm::vec2 velocity;
    };

    // Pushes a moving circle out of overlapping static obstacles in the XZ
    // plane. Any velocity pointing into a contact is removed while the
    // tangential component is preserved, so the object slides along a rock
    // or tree instead of repeatedly driving into it. Several inexpensive
    // solver passes make chains of cliff proxy circles behave as one solid
    // formation rather than allowing one correction to create an overlap
    // with the previous circle.
    static CircleCollisionResult resolveCircleCollisions(
        glm::vec2 position, glm::vec2 velocity, float selfRadius,
        const std::vector<CircleObstacle>& obstacles, int solverIterations = 4);
};
