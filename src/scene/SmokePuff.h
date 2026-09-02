#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// A soft, expanding, fading puff of smoke -- the muzzle blast at the barrel
// and the wisps a flying shell leaves behind it (see Application::
// fireProjectile and the periodic trail spawn in
// Application::updateProjectilesAndCollisions). Unlike ImpactEffect/
// DebrisParticle (which shrink to zero as they die), a puff GROWS as it
// dissipates -- real smoke expands and thins rather than shrinking -- so
// the "fading away" cue here is opacity dropping to 0, not scale.
struct SmokePuff {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};  // slow drift, mostly upward
    float initialScale = 0.4f;
    float finalScale = 1.0f;
    float lifetimeRemaining = 0.45f;
    float initialLifetime = 0.45f;
    bool alive = true;

    void update(float deltaTime) {
        position += velocity * deltaTime;
        lifetimeRemaining -= deltaTime;
        if (lifetimeRemaining <= 0.0f) alive = false;
    }

    glm::mat4 worldMatrix() const {
        float t = 1.0f - glm::clamp(lifetimeRemaining / initialLifetime, 0.0f, 1.0f);
        float scale = glm::mix(initialScale, finalScale, t);
        return glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(scale));
    }

    // Linear fade to fully transparent -- same shrink-to-zero-over-lifetime
    // convention ImpactEffect/DebrisParticle use, just applied to opacity
    // instead of scale here. Capped at 0.55 rather than starting fully
    // opaque -- real smoke is never a solid block of color.
    float opacity() const { return 0.55f * glm::clamp(lifetimeRemaining / initialLifetime, 0.0f, 1.0f); }
};
