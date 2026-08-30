#pragma once

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// A static destructible target. Geometry is a single shared unit-cube mesh
// (owned by whoever draws boxes); each Box just carries a
// position/up/yaw/size/aliveness and positions the shared mesh via
// worldMatrix(), so no per-box GPU buffers are needed.
//
// `up` (the terrain normal at spawn -- see Application::spawnBoxes) tilts
// the crate to sit flush on sloped ground instead of always staying flat,
// the same terrain-alignment idea TrackMark/Tank use; `yaw` is a random
// rotation about that normal for placement variety. Collision (both shell
// hits and the tank driving through, see updateProjectilesAndCollisions)
// still uses the flat axis-aligned aabbMin/Max as an approximation --
// tilting is subtle on this terrain's gentle hills, so the mismatch is
// negligible.
struct Box {
    glm::vec3 position;
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float yaw = 0.0f;
    float size;
    bool alive = true;

    glm::mat4 worldMatrix() const {
        glm::vec3 upN = glm::normalize(up);
        // Build (right, forward) the same verified way TrackMark does:
        // project a reference axis onto the tangent plane of upN to get
        // `forward`, then right = cross(up, forward) -- for flat ground
        // (upN = +Y) this reduces to forward=+Z, right=+X, the identity
        // orientation, and stays a proper right-handed (positive
        // determinant) basis for any tilt. An earlier version derived
        // `right` first via cross(reference, up) then `forward` via
        // cross(up, right), which for upN=+Y works out to right=+Z,
        // forward=+X -- an axis swap with *negative* determinant, i.e. a
        // reflection, which is exactly what was flipping every face's
        // winding and rendering the crate inside-out.
        glm::vec3 reference = std::abs(glm::dot(upN, glm::vec3(0.0f, 0.0f, 1.0f))) < 0.999f
                                   ? glm::vec3(0.0f, 0.0f, 1.0f)
                                   : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 forward = glm::normalize(reference - upN * glm::dot(reference, upN));
        glm::vec3 right = glm::normalize(glm::cross(upN, forward));
        glm::vec3 rotatedRight = std::cos(yaw) * right + std::sin(yaw) * forward;
        glm::vec3 rotatedForward = -std::sin(yaw) * right + std::cos(yaw) * forward;
        return glm::mat4(glm::vec4(rotatedRight * size, 0.0f), glm::vec4(upN * size, 0.0f),
                          glm::vec4(rotatedForward * size, 0.0f), glm::vec4(position, 1.0f));
    }

    glm::vec3 aabbMin() const { return position - glm::vec3(size * 0.5f); }
    glm::vec3 aabbMax() const { return position + glm::vec3(size * 0.5f); }
};
