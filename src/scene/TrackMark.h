#pragma once

#include <glm/glm.hpp>

// One left- or right-tread ground decal, dropped along that track's actual
// world-space path. Its forward vector follows the path tangent rather than
// the hull heading, which is what makes stationary pivot turns leave two
// opposing curved trails. The local terrain normal keeps it slope-aligned.
struct TrackMark {
    glm::vec3 position{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};        // terrain normal at spawn time
    glm::vec3 forward{0.0f, 0.0f, 1.0f};   // trail tangent projected onto the plane of `up`
    float width = 1.5f;
    float length = 1.8f;
    float intensity = 1.0f;  // stronger braking/slip imprints have more alpha
    float lifetimeRemaining = 7.0f;
    float initialLifetime = 7.0f;
    bool alive = true;

    void update(float deltaTime) {
        lifetimeRemaining -= deltaTime;
        if (lifetimeRemaining <= 0.0f) alive = false;
    }

    float opacity() const {
        return intensity * glm::clamp(lifetimeRemaining / initialLifetime, 0.0f, 1.0f);
    }

    glm::mat4 worldMatrix() const {
        glm::vec3 right = glm::normalize(glm::cross(up, forward));
        return glm::mat4(glm::vec4(right * width, 0.0f), glm::vec4(up, 0.0f),
                          glm::vec4(forward * length, 0.0f), glm::vec4(position, 1.0f));
    }
};
