#pragma once

#include <glm/glm.hpp>

// Cap on how many of these can light the scene at once -- mirrored by
// Pipeline::FrameUBO's dynamicLightPosRadius/dynamicLightColorIntensity
// arrays and the matching fixed-size arrays in basic.frag's FrameUBO. Kept
// small: this is a plain per-frame UBO upload, not a real light-culling
// system, and there's rarely more than one or two flashes/explosions live
// at once.
constexpr int kMaxDynamicLights = 4;

// A brief point light spawned by a muzzle flash or explosion -- unlike the
// scene's one directional sun light (FrameUBO::lightDir), this is a local,
// short-lived, unshadowed light purely for visual punch (see basic.frag's
// dynamic-light loop; it's diffuse-only and not ray-traced, so it doesn't
// itself cast shadows). Application::spawnDynamicLight keeps at most
// kMaxDynamicLights of these alive at once, evicting whichever is closest
// to expiring when a new one needs a slot.
struct DynamicLight {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float radius = 5.0f;     // distance at which the contribution reaches zero
    float intensity = 1.0f;  // peak brightness multiplier, at lifetimeRemaining == initialLifetime
    float lifetimeRemaining = 0.2f;
    float initialLifetime = 0.2f;
    bool alive = true;

    void update(float deltaTime) {
        lifetimeRemaining -= deltaTime;
        if (lifetimeRemaining <= 0.0f) alive = false;
    }

    // Linear fade to zero over the light's lifetime -- same shrink-to-zero
    // curve ImpactEffect/DebrisParticle already use for their own fade.
    float currentIntensity() const {
        return intensity * glm::clamp(lifetimeRemaining / initialLifetime, 0.0f, 1.0f);
    }
};
