#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Nested, fixed-size light-space clipmaps. Camera rotation never changes
// their scale/orientation; translation advances in whole shadow texels.
namespace TreeShadowCascades {
inline constexpr uint32_t kCount = 3;
inline constexpr uint32_t kResolution = 2048;
inline constexpr float kDepthRange = 600.f;
inline constexpr std::array<float, kCount> kHalfWidths{18.f, 50.f, 150.f};

inline std::array<glm::mat4, kCount> build(glm::vec3 camera, glm::vec3 lightDirection) {
    glm::vec3 direction = glm::normalize(lightDirection);
    glm::vec3 up = std::abs(direction.y) > .98f ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
    glm::mat4 view = glm::lookAt(-direction * (kDepthRange * .5f), glm::vec3(0), up);
    glm::vec2 center = glm::vec2(view * glm::vec4(camera,1));
    std::array<glm::mat4, kCount> result;
    for (uint32_t i=0; i<kCount; ++i) {
        float half = kHalfWidths[i];
        float texel = 2.f * half / kResolution;
        glm::vec2 snapped = glm::floor(center / texel + .5f) * texel;
        result[i] = glm::orthoRH_ZO(snapped.x-half, snapped.x+half,
                                  snapped.y-half, snapped.y+half, 0.f, kDepthRange) * view;
    }
    return result;
}

// Includes off-camera casters, and the complete light depth interval.
inline bool intersects(const glm::mat4& matrix, glm::vec3 center, float radius) {
    glm::vec3 p = glm::vec3(matrix * glm::vec4(center,1));
    for (int axis=0; axis<3; ++axis) {
        glm::vec3 row(matrix[0][axis], matrix[1][axis], matrix[2][axis]);
        float r = glm::length(row) * radius;
        if (p[axis]+r < (axis == 2 ? 0.f : -1.f) || p[axis]-r > 1.f) return false;
    }
    return true;
}
}
