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
        float age = 1.0f - t;
        // A burst grows: snap out to full size in the first ~20% of life,
        // keep swelling slowly while it fades, then collapse over the last
        // stretch so it doesn't pop out of existence at full size. (The old
        // linear 2.6*t shrink read as a balloon deflating in place.)
        float scale = 2.1f * glm::smoothstep(0.0f, 0.2f, age) * (0.75f + 0.35f * age) *
                      glm::smoothstep(0.0f, 0.3f, t);
        return glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(scale));
    }

    // Drawn alpha-blended so the fireball dissolves rather than vanishing --
    // the HDR-bright flash color stays vivid even as alpha drops.
    float opacity() const {
        float t = glm::clamp(lifetimeRemaining / initialLifetime, 0.0f, 1.0f);
        return glm::smoothstep(0.0f, 0.45f, t);
    }
};
