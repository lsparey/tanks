#pragma once

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// A single flying fragment/spark spawned (in a burst, alongside ImpactEffect's
// flash) when a box is destroyed, to read as an actual explosion rather than
// a static flash. Ember particles are small, fast, unlit (rendered at full
// brightness regardless of scene lighting, like a spark/fire glow) and
// short-lived; chunk particles are bigger, slower, normally lit (tumbling
// debris catching the scene's light/shadow), and live longer -- they bounce
// off the ground, settle briefly as scattered litter, then shrink away.
struct DebrisParticle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 rotationAxis{0.0f, 1.0f, 0.0f};
    float rotationSpeed = 0.0f;  // radians/sec
    float rotationAngle = 0.0f;
    float baseScale = 0.3f;
    float lifetimeRemaining = 1.0f;
    float initialLifetime = 1.0f;
    int meshVariant = 0;  // picks from Application's debrisChunkMeshes_ pool
    bool ember = false;
    bool alive = true;

    // groundHeight: terrain height at the particle's XZ, supplied by the
    // caller (this struct has no terrain access). Fragments bounce with
    // energy loss and settle once too slow to bounce again; sparks that
    // touch the ground gutter out quickly instead of sliding around.
    void update(float deltaTime, float groundHeight) {
        constexpr float kGravity = 9.8f;
        velocity.y -= kGravity * deltaTime;
        position += velocity * deltaTime;
        rotationAngle += rotationSpeed * deltaTime;

        float restY = groundHeight + baseScale * 0.28f;
        if (position.y < restY) {
            position.y = restY;
            if (velocity.y < 0.0f) {
                if (ember) {
                    velocity = glm::vec3(0.0f);
                    rotationSpeed = 0.0f;
                    lifetimeRemaining = std::min(lifetimeRemaining, 0.12f);
                } else if (velocity.y > -1.2f) {  // too slow to bounce -- settle
                    velocity = glm::vec3(0.0f);
                    rotationSpeed = 0.0f;
                } else {
                    velocity.y = -velocity.y * 0.4f;
                    velocity.x *= 0.55f;
                    velocity.z *= 0.55f;
                    rotationSpeed *= 0.6f;
                }
            }
        }

        lifetimeRemaining -= deltaTime;
        if (lifetimeRemaining <= 0.0f) alive = false;
    }

    glm::mat4 worldMatrix() const {
        float t = glm::clamp(lifetimeRemaining / initialLifetime, 0.0f, 1.0f);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, rotationAngle, rotationAxis);
        // Embers shrink over their whole (short) life like a spark burning
        // out. Chunks hold full size while flying/settled and only shrink
        // over the last stretch -- the old full-lifetime shrink made every
        // fragment visibly deflate mid-flight, which is what read as
        // "placeholder particle" rather than debris.
        float sizeFade = ember ? t : glm::smoothstep(0.0f, 0.3f, t);
        return glm::scale(m, glm::vec3(baseScale * sizeFade));
    }
};
