#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// A brief, shrinking flash spawned at the hit point when a box is
// destroyed -- purely visual feedback, no gameplay effect.
struct ImpactEffect {
    glm::vec3 position;
    float lifetimeRemaining = 0.3f;
    float initialLifetime = 0.3f;
    bool alive = true;

    void update(float deltaTime) {
        lifetimeRemaining -= deltaTime;
        if (lifetimeRemaining <= 0.0f) alive = false;
    }

    glm::mat4 worldMatrix() const {
        float t = glm::clamp(lifetimeRemaining / initialLifetime, 0.0f, 1.0f);
        // Bigger than the box it replaces and shrinking fast, so it reads as
        // a distinct burst rather than "the box shrinking in place."
        float scale = 2.6f * t;
        return glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(scale));
    }
};
