#pragma once

#include <glm/glm.hpp>

class InputManager;

// Free-fly camera for M3 (WASD + mouse look). Gains a follow mode in M5 once
// there's a Tank to follow.
class Camera {
public:
    explicit Camera(glm::vec3 position = glm::vec3(0.0f, 8.0f, 15.0f), float yawDegrees = -90.0f,
                     float pitchDegrees = -20.0f);

    void update(const InputManager& input, float deltaTime);

    // Chase camera: positions the camera behind and above the target along
    // -targetForward, looking at the target. Bypasses free-fly movement/look
    // entirely while active; switching back to update() resumes free-fly
    // from wherever followTarget last left the camera.
    void followTarget(glm::vec3 targetPosition, glm::vec3 targetForward, float distance = 8.0f,
                       float height = 3.5f);

    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix(float aspect) const;
    glm::vec3 position() const { return position_; }

private:
    void updateBasisVectors();

    glm::vec3 position_;
    glm::vec3 front_{0.0f, 0.0f, -1.0f};
    glm::vec3 right_{1.0f, 0.0f, 0.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    float yawDegrees_;
    float pitchDegrees_;

    float moveSpeed_ = 5.0f;
    float mouseSensitivity_ = 0.1f;
    float fovDegrees_ = 60.0f;
};
