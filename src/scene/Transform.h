#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Plain position/rotation(Euler, radians)/scale bundle. No parent/child
// hierarchy needed at this prototype's scope.
struct Transform {
    glm::vec3 position{0.0f};
    glm::vec3 eulerAngles{0.0f};  // pitch (x), yaw (y), roll (z), radians
    float scale = 1.0f;

    glm::mat4 matrix() const {
        glm::mat4 m(1.0f);
        m = glm::translate(m, position);
        m = glm::rotate(m, eulerAngles.y, glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::rotate(m, eulerAngles.x, glm::vec3(1.0f, 0.0f, 0.0f));
        m = glm::rotate(m, eulerAngles.z, glm::vec3(0.0f, 0.0f, 1.0f));
        m = glm::scale(m, glm::vec3(scale));
        return m;
    }
};
