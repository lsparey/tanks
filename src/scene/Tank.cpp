#include "Tank.h"

#include <cmath>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "../io/ModelLoader.h"
#include "InputManager.h"
#include "Terrain.h"

Tank::Tank(VulkanContext& ctx, CommandContext& commands, const std::string& modelPath)
    : mesh_(loadMesh(ctx, commands, modelPath)) {}

Mesh Tank::loadMesh(VulkanContext& ctx, CommandContext& commands, const std::string& path) {
    ModelLoader::Result result = ModelLoader::load(path);
    return Mesh(ctx, commands, result.vertices, result.indices);
}

void Tank::update(const InputManager& input, float deltaTime, const Terrain& terrain) {
    float throttle = 0.0f;
    if (input.isKeyDown(GLFW_KEY_W)) throttle += 1.0f;
    if (input.isKeyDown(GLFW_KEY_S)) throttle -= 1.0f;

    float turn = 0.0f;
    if (input.isKeyDown(GLFW_KEY_A)) turn += 1.0f;
    if (input.isKeyDown(GLFW_KEY_D)) turn -= 1.0f;

    yaw_ += turn * turnSpeedRadians_ * deltaTime;

    glm::vec3 flatForward(std::sin(yaw_), 0.0f, std::cos(yaw_));
    position_ += flatForward * (throttle * moveSpeed_ * deltaTime);

    glm::vec3 normal = terrain.normalAt(position_.x, position_.z);
    position_.y = terrain.heightAt(position_.x, position_.z);

    // Re-derive an orthonormal basis each frame: up is the terrain normal,
    // forward is the driver-controlled flat heading projected onto the
    // terrain plane, right completes a right-handed set consistent with the
    // model's own local axes (+X right, +Y up, +Z forward).
    up_ = normal;
    forward_ = glm::normalize(flatForward - up_ * glm::dot(flatForward, up_));
    right_ = glm::normalize(glm::cross(up_, forward_));
}

glm::mat4 Tank::worldMatrix() const {
    glm::mat4 basis(glm::vec4(right_, 0.0f), glm::vec4(up_, 0.0f), glm::vec4(forward_, 0.0f),
                     glm::vec4(position_, 1.0f));
    return basis * modelCorrection_;
}
