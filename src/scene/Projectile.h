#pragma once

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// A fired shell: constant-velocity straight-line motion with a lifetime
// timeout. previousPosition is kept each frame so collision can be tested
// against the swept segment, not just the new point (see CollisionSystem).
struct Projectile {
    glm::vec3 position;
    glm::vec3 previousPosition;
    glm::vec3 velocity;
    float lifetimeRemaining = 3.0f;
    bool alive = true;
    // Distance travelled since the last smoke-trail puff was dropped --
    // reset by whoever spawns a puff (see Application::updateProjectilesAndCollisions),
    // same "accumulate until a spacing threshold, then reset" idea as
    // Application::updateTrackMarks uses for the tank's own tread marks,
    // just tracked per-shell here since multiple can be in flight at once.
    float distanceSinceLastPuff = 0.0f;

    void update(float deltaTime) {
        previousPosition = position;
        position += velocity * deltaTime;
        lifetimeRemaining -= deltaTime;
        if (lifetimeRemaining <= 0.0f) alive = false;
    }

    glm::mat4 worldMatrix() const {
        // Orient Mesh::shell's local +Z (the nose direction it's built
        // along) to point down the shell's actual flight direction, same
        // arbitrary-perpendicular-basis construction buildTreeBranch uses
        // for oriented frustums -- a flat-out (near-zero velocity, e.g. the
        // instant after spawn if update() hasn't run yet) falls back to a
        // fixed forward so normalize() never divides by ~0.
        glm::vec3 fwd = glm::length(velocity) > 0.001f ? glm::normalize(velocity)
                                                         : glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 worldUp = std::abs(fwd.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                       : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(worldUp, fwd));
        glm::vec3 up = glm::cross(fwd, right);
        return glm::mat4(glm::vec4(right, 0.0f), glm::vec4(up, 0.0f), glm::vec4(fwd, 0.0f),
                          glm::vec4(position, 1.0f));
    }
};
