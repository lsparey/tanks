#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../render/AccelerationStructure.h"
#include "../render/CommandContext.h"
#include "../render/Mesh.h"
#include "../render/VulkanContext.h"
#include "CollisionSystem.h"

class InputManager;
class Terrain;

// The player tank, loaded from a model file via ModelLoader/Assimp.
//
// The source .x file has no Frame/node hierarchy, but it does have five
// named materials (Tracks, Base, Detail, Turret, Barrel) -- Assimp always
// splits a mesh into one sub-mesh per material, so the turret, barrel, and
// tracks all come out as separate geometry automatically. "Base"/"Detail"
// (the only two without their own DrawPart) are merged into one rigid hull
// mesh; the turret (and the barrel riding on it) get their own yaw, driven
// by Q/E, applied as a rotation about the model's local Y axis before the
// hull's own placement -- the same scheme (and the same hardcoded
// pivot-at-origin assumption) used by the original DirectX9 project this
// asset came from. The tracks share the hull's own placement (they don't
// move independently in this prototype).
class Tank {
public:
    struct DrawPart {
        const Mesh* mesh;
        glm::mat4 worldMatrix;
        VkDeviceAddress blasAddress;
        // Bare metal (tracks, barrel) vs. painted camo (hull, turret) -- see
        // Application's camoMaterialSet_/metalMaterialSet_, bound per-part
        // in the tank draw loop based on this flag.
        bool metallic = false;
    };

    Tank(VulkanContext& ctx, CommandContext& commands, const std::string& modelPath);

    // Track-driven movement (W/S throttle, A/D differential steering) plus
    // turret traverse (Q/E, independent of hull yaw). Movement is simulated
    // in fixed-size substeps with acceleration, braking, traction, slope
    // gravity, and velocity-aware collision response. `obstacles`
    // (trees/rocks, see Application::obstacles_) approximates irregular
    // silhouettes as circles in the XZ plane. `boundaryHalfExtent` likewise
    // keeps the hull behind the play-area boundary's wall of light.
    void update(const InputManager& input, float deltaTime, const Terrain& terrain,
                const std::vector<CollisionSystem::CircleObstacle>& obstacles,
                float boundaryHalfExtent);

    // One entry per renderable part with its own world matrix -- just the
    // hull if the model had no separate turret/barrel materials, otherwise
    // hull + turret + barrel.
    std::vector<DrawPart> drawParts() const;

    glm::vec3 position() const { return position_; }
    glm::vec3 forward() const { return forward_; }
    // Hull's local-space X extent (outer edge to outer edge) -- see load().
    float hullWidth() const { return hullWidth_; }
    // Firing/aim direction: hull forward rotated by the turret's yaw.
    // Equal to forward() if the model had no separate turret to rotate.
    glm::vec3 aimDirection() const;
    // World-space position of the barrel's muzzle tip, tracking the
    // turret's current yaw. Falls back to an approximate point along
    // aimDirection() if the model had no separate barrel material.
    glm::vec3 muzzleWorldPosition() const;

    // Baked-in correction for the source model's own axes/scale/pivot,
    // applied before the computed world orientation. Tuned once by
    // inspection after first seeing the model on screen.
    glm::mat4& modelCorrection() { return modelCorrection_; }

private:
    void load(VulkanContext& ctx, CommandContext& commands, const std::string& path);
    void simulateMovement(float throttle, float turn, float deltaTime, const Terrain& terrain,
                          const std::vector<CollisionSystem::CircleObstacle>& obstacles,
                          float boundaryHalfExtent);
    void updateGroundPose(const Terrain& terrain);
    void updateSuspensionPose(const Terrain& terrain, float deltaTime,
                              float longitudinalAcceleration, float lateralAcceleration);
    glm::mat4 hullWorldMatrix() const;
    glm::mat4 turretWorldMatrix() const;

    std::unique_ptr<Mesh> hullMesh_;
    std::unique_ptr<Mesh> turretMesh_;  // null if the model had no separate turret material
    std::unique_ptr<Mesh> barrelMesh_;  // null if the model had no separate barrel material
    std::unique_ptr<Mesh> trackMesh_;   // null if the model had no separate tracks material
    // BLAS per rigid part, built once at load time alongside the meshes
    // above -- geometry never deforms, only the per-frame world matrix
    // (from hullWorldMatrix()/turretWorldMatrix()) changes.
    std::unique_ptr<AccelerationStructure> hullBLAS_;
    std::unique_ptr<AccelerationStructure> turretBLAS_;
    std::unique_ptr<AccelerationStructure> barrelBLAS_;
    std::unique_ptr<AccelerationStructure> trackBLAS_;
    // Local-space muzzle tip, valid only when barrelMesh_ is non-null: the
    // barrel part's vertex farthest from the local origin (the turret's
    // pivot, which sits near the barrel's mount/breech end, not its tip).
    glm::vec3 muzzleLocal_{0.0f};
    float hullWidth_ = 0.0f;
    float hullLength_ = 0.0f;

    // Ground-constrained gameplay pose, used for movement, collision, the
    // follow camera, and track-mark placement.
    glm::vec3 position_{0.0f};
    float yaw_ = 0.0f;  // radians; yaw=0 means local forward (+Z) points world +Z
    glm::vec3 forward_{0.0f, 0.0f, 1.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    glm::vec3 right_{1.0f, 0.0f, 0.0f};

    // Spring-damped render pose derived from four terrain contact samples.
    // It can pitch, roll, and heave without feeding visual oscillation back
    // into the stable planar movement simulation above.
    glm::vec3 visualPosition_{0.0f};
    glm::vec3 visualForward_{0.0f, 0.0f, 1.0f};
    glm::vec3 visualUp_{0.0f, 1.0f, 0.0f};
    glm::vec3 visualRight_{1.0f, 0.0f, 0.0f};
    float suspensionHeight_ = 0.0f;
    float suspensionHeightVelocity_ = 0.0f;
    float suspensionPitch_ = 0.0f;
    float suspensionPitchVelocity_ = 0.0f;
    float suspensionRoll_ = 0.0f;
    float suspensionRollVelocity_ = 0.0f;
    bool suspensionInitialized_ = false;

    // Planar rigid-body state. The tank remains constrained to the terrain
    // surface, while its XZ velocity and yaw velocity carry momentum between
    // frames. A world-space velocity lets collision response preserve the
    // component tangent to an obstacle instead of losing all movement.
    glm::vec2 velocity_{0.0f};
    float angularVelocity_ = 0.0f;
    float movementAccumulator_ = 0.0f;

    float turretYaw_ = 0.0f;  // radians, relative to the hull, about local +Y

    float turretTurnSpeedRadians_ = 0.6f;    // rad/s, turret -- slower than hull for finer aiming

    glm::mat4 modelCorrection_{1.0f};
};
