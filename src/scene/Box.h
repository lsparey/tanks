#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// A static, axis-aligned destructible target. Geometry is a single shared
// unit-cube mesh (owned by whoever draws boxes); each Box just carries a
// position/size/aliveness and scales the shared mesh via worldMatrix(), so
// no per-box GPU buffers are needed.
struct Box {
    glm::vec3 position;
    float size;
    bool alive = true;

    glm::mat4 worldMatrix() const {
        return glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(size));
    }

    glm::vec3 aabbMin() const { return position - glm::vec3(size * 0.5f); }
    glm::vec3 aabbMax() const { return position + glm::vec3(size * 0.5f); }
};
