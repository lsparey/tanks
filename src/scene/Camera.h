#pragma once

#include <glm/glm.hpp>

class InputManager;

// Shared free-fly and tank-follow camera. Application selects whether it
// follows the hull, frames a point along the turret's aim line, or accepts
// direct free-camera input.
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

    // Closer aiming view: orbit behind the horizontal turret direction and
    // look directly at aimPoint. Passing the same aim point used by the HUD
    // keeps its projected crosshair centered in this mode.
    void followAimTarget(glm::vec3 targetPosition, glm::vec3 aimPoint, float distance = 5.8f,
                         float height = 2.8f);

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
