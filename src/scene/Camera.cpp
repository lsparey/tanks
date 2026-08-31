#include "Camera.h"

#include <cmath>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "InputManager.h"

Camera::Camera(glm::vec3 position, float yawDegrees, float pitchDegrees)
    : position_(position), yawDegrees_(yawDegrees), pitchDegrees_(pitchDegrees) {
    updateBasisVectors();
}

void Camera::update(const InputManager& input, float deltaTime) {
    glm::vec2 delta = input.mouseDelta();
    yawDegrees_ += delta.x * mouseSensitivity_;
    pitchDegrees_ -= delta.y * mouseSensitivity_;
    pitchDegrees_ = glm::clamp(pitchDegrees_, -89.0f, 89.0f);
    updateBasisVectors();

    // Arrow keys, not WASD: WASD drives the tank once Tank::update exists
    // (from M6 onward), so free-fly movement uses a disjoint key set.
    float velocity = moveSpeed_ * deltaTime;
    if (input.isKeyDown(GLFW_KEY_UP)) position_ += front_ * velocity;
    if (input.isKeyDown(GLFW_KEY_DOWN)) position_ -= front_ * velocity;
    if (input.isKeyDown(GLFW_KEY_LEFT)) position_ -= right_ * velocity;
    if (input.isKeyDown(GLFW_KEY_RIGHT)) position_ += right_ * velocity;
    if (input.isKeyDown(GLFW_KEY_SPACE)) position_ += up_ * velocity;
    if (input.isKeyDown(GLFW_KEY_LEFT_CONTROL)) position_ -= up_ * velocity;
}

void Camera::followTarget(glm::vec3 targetPosition, glm::vec3 targetForward, float distance,
                           float height) {
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    position_ = targetPosition - targetForward * distance + worldUp * height;
    glm::vec3 lookAt = targetPosition + worldUp * 1.0f;
    front_ = glm::normalize(lookAt - position_);
    right_ = glm::normalize(glm::cross(front_, worldUp));
    up_ = glm::normalize(glm::cross(right_, front_));
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position_, position_ + front_, up_);
}

glm::mat4 Camera::projMatrix(float aspect) const {
    // Far plane comfortably exceeds the terrain's corner-to-corner diagonal
    // (worldSize 180 -> ~255) so distant terrain doesn't get clipped when
    // looking across the map.
    return glm::perspective(glm::radians(fovDegrees_), aspect, 0.1f, 500.0f);
}

void Camera::updateBasisVectors() {
    glm::vec3 front;
    front.x = std::cos(glm::radians(yawDegrees_)) * std::cos(glm::radians(pitchDegrees_));
    front.y = std::sin(glm::radians(pitchDegrees_));
    front.z = std::sin(glm::radians(yawDegrees_)) * std::cos(glm::radians(pitchDegrees_));
    front_ = glm::normalize(front);
    right_ = glm::normalize(glm::cross(front_, glm::vec3(0.0f, 1.0f, 0.0f)));
    up_ = glm::normalize(glm::cross(right_, front_));
}
