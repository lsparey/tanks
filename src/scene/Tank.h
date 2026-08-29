#pragma once

#include <string>

#include "../render/CommandContext.h"
#include "../render/Mesh.h"
#include "../render/VulkanContext.h"

class InputManager;
class Terrain;

// The player tank, loaded from a model file via ModelLoader/Assimp. Treated
// as a single rigid mesh: the source .x file has no frame hierarchy or
// joints (verified by inspection), so there's no separately-animatable
// turret/gun to drive -- the whole hull rotates together to aim.
//
// Orientation isn't a generic Transform (Euler angles): the hull needs to
// stay aligned to the terrain normal under it while yaw remains driver-
// controlled, so it's built directly from an explicit forward/up/right
// basis each frame instead.
class Tank {
public:
    Tank(VulkanContext& ctx, CommandContext& commands, const std::string& modelPath);

    void bindAndDraw(VkCommandBuffer cmd) const { mesh_.bindAndDraw(cmd); }

    // Arcade steering (W/S throttle, A/D yaw) plus terrain ground-clamping:
    // samples terrain height/normal under the hull and aligns pitch/roll to
    // the slope while yaw stays under driver control.
    void update(const InputManager& input, float deltaTime, const Terrain& terrain);

    glm::vec3 position() const { return position_; }
    glm::vec3 forward() const { return forward_; }

    glm::mat4 worldMatrix() const;

    // Baked-in correction for the source model's own axes/scale/pivot,
    // applied before the computed world orientation. Tuned once by
    // inspection after first seeing the model on screen.
    glm::mat4& modelCorrection() { return modelCorrection_; }

private:
    static Mesh loadMesh(VulkanContext& ctx, CommandContext& commands, const std::string& path);

    Mesh mesh_;

    glm::vec3 position_{0.0f};
    float yaw_ = 0.0f;  // radians; yaw=0 means local forward (+Z) points world +Z
    glm::vec3 forward_{0.0f, 0.0f, 1.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    glm::vec3 right_{1.0f, 0.0f, 0.0f};

    float moveSpeed_ = 6.0f;           // m/s
    float turnSpeedRadians_ = 1.2f;    // rad/s

    glm::mat4 modelCorrection_{1.0f};
};
