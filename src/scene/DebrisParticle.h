#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// A single flying chunk/spark spawned (in a burst, alongside ImpactEffect's
// flash) when a box is destroyed, to read as an actual explosion rather than
// a static flash. Ember particles are small, fast, unlit (rendered at full
// brightness regardless of scene lighting, like a spark/fire glow) and
// short-lived; chunk particles are bigger, slower, normally lit (tumbling
// debris catching the scene's light/shadow), and live a bit longer as they
// fall.
struct DebrisParticle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 rotationAxis{0.0f, 1.0f, 0.0f};
    float rotationSpeed = 0.0f;  // radians/sec
    float rotationAngle = 0.0f;
    float baseScale = 0.3f;
    float lifetimeRemaining = 1.0f;
    float initialLifetime = 1.0f;
    bool ember = false;
    bool alive = true;

    void update(float deltaTime) {
        constexpr float kGravity = 9.8f;
        velocity.y -= kGravity * deltaTime;
        position += velocity * deltaTime;
        rotationAngle += rotationSpeed * deltaTime;
        lifetimeRemaining -= deltaTime;
        if (lifetimeRemaining <= 0.0f) alive = false;
    }

    glm::mat4 worldMatrix() const {
        float t = glm::clamp(lifetimeRemaining / initialLifetime, 0.0f, 1.0f);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, rotationAngle, rotationAxis);
        // Shrinks to nothing over its lifetime instead of popping out of
        // existence, which reads as debris burning up/settling rather than
        // an abrupt disappearance.
        return glm::scale(m, glm::vec3(baseScale * t));
    }
};
