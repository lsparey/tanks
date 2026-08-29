#pragma once

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

    void update(float deltaTime) {
        previousPosition = position;
        position += velocity * deltaTime;
        lifetimeRemaining -= deltaTime;
        if (lifetimeRemaining <= 0.0f) alive = false;
    }

    glm::mat4 worldMatrix() const {
        return glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(0.15f));
    }
};
