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

    // Arcade steering (W/S throttle, A/D yaw) plus terrain ground-clamping,
    // plus turret traverse (Q/E, independent of hull yaw). `obstacles`
    // (trees/rocks, see Application::obstacles_) blocks the hull from
    // driving through them -- approximated as circles in the XZ plane
    // rather than their real irregular silhouettes. `boundaryHalfExtent`
    // (see Application::boundaryHalfExtent_/BoundaryGenerator) likewise
    // keeps the hull from driving through the play-area boundary's wall of
    // light.
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

    glm::vec3 position_{0.0f};
    float yaw_ = 0.0f;  // radians; yaw=0 means local forward (+Z) points world +Z
    glm::vec3 forward_{0.0f, 0.0f, 1.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    glm::vec3 right_{1.0f, 0.0f, 0.0f};

    float turretYaw_ = 0.0f;  // radians, relative to the hull, about local +Y

    float moveSpeed_ = 6.0f;                 // m/s
    float turnSpeedRadians_ = 1.2f;          // rad/s, hull
    float turretTurnSpeedRadians_ = 1.5f;    // rad/s, turret

    glm::mat4 modelCorrection_{1.0f};
};
