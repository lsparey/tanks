#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// A purely decorative, static tree placement. Geometry is a single shared
// tree mesh (see Mesh::tree); each instance just carries a
// position/yaw/scale and positions the shared mesh via worldMatrix().
struct TreeInstance {
    glm::vec3 position;
    float yaw = 0.0f;
    float scale = 1.0f;

    glm::mat4 worldMatrix() const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        return glm::scale(m, glm::vec3(scale));
    }
};
