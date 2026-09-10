#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// A purely decorative, static grass tuft placement -- same shape as
// ShrubInstance (position/yaw/scale/meshVariant + worldMatrix()), just for
// Mesh::grassClump's blade tufts instead of shrub blobs. Deliberately NOT
// added to the ray-traced TLAS (see Application::gatherRayTracingInstances'
// comment on debris/track marks/shrubs for the same reasoning) -- there are
// meant to be far more of these scattered around than any other decorative
// prop, and individually shadow-casting each one isn't worth the TLAS churn
// for something this small.
struct GrassClumpInstance {
    glm::vec3 position;
    float yaw = 0.0f;
    float scale = 1.0f;
    int meshVariant = 0;

    glm::mat4 worldMatrix() const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        return glm::scale(m, glm::vec3(scale));
    }
};
