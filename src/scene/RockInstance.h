#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// A single static rock, placed as part of a cluster (see
// Application::spawnRocks). meshVariant indexes into Application's small
// pool of procedurally distinct rock shapes (Mesh::rock) so a cluster
// doesn't read as one shape copy-pasted several times.
struct RockInstance {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float scale = 1.0f;
    int meshVariant = 0;
    int lod = 0;  // regular boulders only; decorative scree always uses LOD 2

    glm::mat4 worldMatrix() const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        return glm::scale(m, glm::vec3(scale));
    }
};
