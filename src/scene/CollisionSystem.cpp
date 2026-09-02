#include "CollisionSystem.h"

#include <algorithm>
#include <cmath>

bool CollisionSystem::segmentIntersectsAABB(glm::vec3 from, glm::vec3 to, glm::vec3 boxMin,
                                             glm::vec3 boxMax, float* outT) {
    glm::vec3 dir = to - from;
    float tmin = 0.0f;
    float tmax = 1.0f;

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(dir[axis]) < 1e-8f) {
            // Segment is parallel to this axis's slab: it only passes
            // through if the origin already lies within the slab.
            if (from[axis] < boxMin[axis] || from[axis] > boxMax[axis]) return false;
            continue;
        }
        float invD = 1.0f / dir[axis];
        float t1 = (boxMin[axis] - from[axis]) * invD;
        float t2 = (boxMax[axis] - from[axis]) * invD;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (outT) *outT = tmin;
    return true;
}

bool CollisionSystem::segmentIntersectsSphere(glm::vec3 from, glm::vec3 to, glm::vec3 center,
                                               float radius, float* outT) {
    glm::vec3 d = to - from;
    glm::vec3 f = from - center;
    float a = glm::dot(d, d);
    if (a < 1e-8f) {
        // Degenerate (near-zero-length) segment -- a point-in-sphere check.
        if (glm::dot(f, f) > radius * radius) return false;
        if (outT) *outT = 0.0f;
        return true;
    }
    float b = 2.0f * glm::dot(f, d);
    float c = glm::dot(f, f) - radius * radius;
    float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) return false;  // line misses the sphere entirely

    float sqrtDisc = std::sqrt(discriminant);
    float t = (-b - sqrtDisc) / (2.0f * a);  // earlier (entry) root first
    if (t < 0.0f) t = (-b + sqrtDisc) / (2.0f * a);  // segment started inside the sphere
    if (t < 0.0f || t > 1.0f) return false;

    if (outT) *outT = t;
    return true;
}

glm::vec2 CollisionSystem::resolveCircleCollisions(glm::vec2 position, float selfRadius,
                                                    const std::vector<CircleObstacle>& obstacles) {
    for (const auto& obstacle : obstacles) {
        glm::vec2 delta = position - obstacle.center;
        float dist = glm::length(delta);
        float minDist = selfRadius + obstacle.radius;
        if (dist < minDist) {
            // Degenerate case (dead center on the obstacle, dist ~ 0) has no
            // well-defined push direction -- push along an arbitrary fixed
            // axis rather than producing a NaN from normalizing a zero
            // vector.
            glm::vec2 pushDir = dist > 1e-5f ? delta / dist : glm::vec2(1.0f, 0.0f);
            position = obstacle.center + pushDir * minDist;
        }
    }
    return position;
}
