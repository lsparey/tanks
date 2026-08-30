#pragma once

#include <glm/glm.hpp>

// A single flat ground decal dropped periodically behind the moving tank
// (see Application::updateTrackMarks), fading out linearly over its
// lifetime before being removed. Built directly from the tank's forward
// vector and the local terrain normal at spawn time rather than a stored
// yaw angle, matching the basis-construction style Tank::hullWorldMatrix
// already uses -- tilting to the terrain normal (instead of assuming flat
// ground) keeps the decal from poking through sloped terrain.
struct TrackMark {
    glm::vec3 position{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};        // terrain normal at spawn time
    glm::vec3 forward{0.0f, 0.0f, 1.0f};   // tank forward, projected onto the tangent plane of `up`
    float width = 1.5f;
    float length = 1.8f;
    float lifetimeRemaining = 9.0f;
    float initialLifetime = 9.0f;
    bool alive = true;

    void update(float deltaTime) {
        lifetimeRemaining -= deltaTime;
        if (lifetimeRemaining <= 0.0f) alive = false;
    }

    float opacity() const { return glm::clamp(lifetimeRemaining / initialLifetime, 0.0f, 1.0f); }

    glm::mat4 worldMatrix() const {
        glm::vec3 right = glm::normalize(glm::cross(up, forward));
        return glm::mat4(glm::vec4(right * width, 0.0f), glm::vec4(up, 0.0f),
                          glm::vec4(forward * length, 0.0f), glm::vec4(position, 1.0f));
    }
};
