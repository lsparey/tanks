#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../render/AccelerationStructure.h"
#include "../render/CommandContext.h"
#include "../render/Mesh.h"
#include "../render/VulkanContext.h"
#include "CollisionSystem.h"
#include "RunningGear.h"

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
// hull's own placement. The barrel additionally pitches around a trunnion
// derived from its breech geometry. The refined OBJ's named wheels and
// shoes are extracted into instanced batches with independent per-side travel.
// Legacy models without that rig retain their original rigid tracks.
class Tank {
public:
    // Matches the tank material branch in basic.frag.
    enum class Surface { Armour = 5, Tracks = 6, Barrel = 7 };
    // Which of Tank's world/prevWorldMatrix() pairs this part's worldMatrix
    // actually came from -- independent of Surface, since e.g. Surface::
    // Tracks covers both a hull-attached mesh (trackMesh_) and a turret-
    // attached one (turretDetailMesh_). Lets the motion-vector shader code
    // look up the right previous matrix without guessing from materialType.
    enum class PoseGroup { Hull, Turret, Barrel };
    struct DrawPart {
        const Mesh* mesh;
        glm::mat4 worldMatrix;
        VkDeviceAddress blasAddress;
        Surface surface = Surface::Armour;
        PoseGroup poseGroup = PoseGroup::Hull;
    };
    struct GearBatch {
        std::unique_ptr<Mesh> mesh;
        std::unique_ptr<AccelerationStructure> blas;
        Surface surface = Surface::Tracks;
    };
    const std::vector<GearBatch>& gearBatches() const { return gearBatches_; }
    std::vector<std::vector<glm::mat4>> gearTransforms() const {
        return runningGear_.transforms(hullWorldMatrix());
    }
    glm::mat4 worldToHull() const { return glm::inverse(hullWorldMatrix()); }

    Tank(VulkanContext& ctx, CommandContext& commands, const std::string& modelPath,
         bool animateTracks = true);

    // Track-driven movement (W/S throttle, A/D differential steering) plus
    // turret traverse (Q/E, independent of hull yaw). Movement is simulated
    // in fixed-size substeps with acceleration, braking, traction, slope
    // gravity, and velocity-aware collision response. The hull uses an
    // oriented capsule against the circular tree/rock proxies in `obstacles`
    // (see Application::obstacles_). `boundaryHalfExtent` likewise keeps the
    // complete oriented hull behind the play-area boundary's wall of light.
    void update(const InputManager& input, float deltaTime, const Terrain& terrain,
                const std::vector<CollisionSystem::CircleObstacle>& obstacles,
                float boundaryHalfExtent);

    // Rigid parts. Instanced running gear is exposed separately so raster
    // batches and ray instances can share the same computed transforms.
    std::vector<DrawPart> drawParts() const;

    glm::vec3 position() const { return position_; }
    glm::vec3 forward() const { return forward_; }
    // Hull's local-space X extent (outer edge to outer edge) -- see load().
    float hullWidth() const { return hullWidth_; }
    float hullLength() const { return hullLength_; }
    // Initial placement uses the accepted ground and first route heading.
    void placeAt(glm::vec3 position, glm::vec2 forward, const Terrain& terrain);
    glm::vec4 surfaceBounds() const { return surfaceBounds_; }
    // Snapshotted at the start of update(), before this frame's integration
    // mutates the pose -- i.e. "the world matrix as it was last frame".
    // Hull/turret/barrel pose comes from a stateful spring/rigid-body
    // simulation (see springTowards()), not a pure function of time, so
    // unlike wind bending this can't be recomputed analytically for an
    // arbitrary past instant; it has to be captured explicitly each frame.
    // Used for the rendered motion-vector buffer (see basic.vert/frag).
    glm::mat4 prevHullWorldMatrix() const { return prevHullMatrix_; }
    glm::mat4 prevTurretWorldMatrix() const { return prevTurretMatrix_; }
    glm::mat4 prevBarrelWorldMatrix() const { return prevBarrelMatrix_; }
    // Physical ground-contact locations used by independent tread trails.
    // They follow the stable gameplay pose rather than the oscillating
    // suspension render pose.
    glm::vec3 leftTrackGroundPosition() const;
    glm::vec3 rightTrackGroundPosition() const;
    float leftTrackGroundSpeed() const;
    float rightTrackGroundSpeed() const;
    float leftTrackContactAmount() const { return leftTrackContactAmount_; }
    float rightTrackContactAmount() const { return rightTrackContactAmount_; }
    float trackWidth() const { return hullWidth_ * 0.18f; }
    float longitudinalAcceleration() const { return longitudinalAcceleration_; }
    float lateralSlipSpeed() const { return lateralSlipSpeed_; }
    float angularSpeed() const { return std::abs(angularVelocity_); }
    // Firing/aim direction: hull forward rotated by the turret's yaw and
    // the main gun's elevation. Equal to forward() if the model had no
    // separate turret or barrel to rotate.
    glm::vec3 aimDirection() const;
    // World-space position of the barrel's muzzle tip, tracking the
    // turret's current yaw. Falls back to an approximate point along
    // aimDirection() if the model had no separate barrel material.
    glm::vec3 muzzleWorldPosition() const;

    // Start a firing response: snap the barrel rearward into its return
    // spring and add a small opposite impulse to the planar hull velocity.
    // Projectile/muzzle effects should be spawned before calling this so
    // they originate at the muzzle's pre-recoil position.
    void applyGunRecoil();

    // Baked-in correction for the source model's own axes/scale/pivot,
    // applied before the computed world orientation. Tuned once by
    // inspection after first seeing the model on screen.
    glm::mat4& modelCorrection() { return modelCorrection_; }

private:
    void load(VulkanContext& ctx, CommandContext& commands, const std::string& path, bool animateTracks);
    void simulateMovement(float throttle, float turn, float deltaTime, const Terrain& terrain,
                          const std::vector<CollisionSystem::CircleObstacle>& obstacles,
                          float boundaryHalfExtent);
    void updateGroundPose(const Terrain& terrain);
    void updateSuspensionPose(const Terrain& terrain, float deltaTime,
                              float longitudinalAcceleration, float lateralAcceleration);
    void updateGunRecoil(float deltaTime);
    glm::mat4 hullWorldMatrix() const;
    glm::mat4 turretWorldMatrix() const;
    glm::mat4 barrelWorldMatrix() const;

    std::unique_ptr<Mesh> hullMesh_;
    std::unique_ptr<Mesh> turretMesh_;  // null if the model had no separate turret material
    std::unique_ptr<Mesh> barrelMesh_;  // null if the model had no separate barrel material
    std::unique_ptr<Mesh> trackMesh_;   // null if the model had no separate tracks material
    std::unique_ptr<Mesh> turretDetailMesh_;  // dark optics/grilles, rigidly attached to turret
    // BLAS per rigid part, built once at load time alongside the meshes
    // above -- geometry never deforms, only the per-frame world matrix
    // (from hullWorldMatrix()/turretWorldMatrix()) changes.
    std::unique_ptr<AccelerationStructure> hullBLAS_;
    std::unique_ptr<AccelerationStructure> turretBLAS_;
    std::unique_ptr<AccelerationStructure> barrelBLAS_;
    std::unique_ptr<AccelerationStructure> trackBLAS_;
    std::unique_ptr<AccelerationStructure> turretDetailBLAS_;
    RunningGear::Rig runningGear_;
    std::vector<GearBatch> gearBatches_;
    // Local-space muzzle tip, valid only when barrelMesh_ is non-null: the
    // centre of the barrel's forward-most cross-section.
    glm::vec3 muzzleLocal_{0.0f};
    // Local trunnion at the center of the barrel's breech-end cross-section.
    // Elevation rotates the barrel around this point rather than orbiting it
    // around the model origin.
    glm::vec3 barrelPivotLocal_{0.0f};
    float hullWidth_ = 0.0f;
    float hullLength_ = 0.0f;
    // Hull bottom/inverse height and muzzle Z/inverse barrel length, in the
    // authored model space used by the procedural dust and soot masks.
    glm::vec4 surfaceBounds_{0.0f};

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
    float leftTrackContactAmount_ = 1.0f;
    float rightTrackContactAmount_ = 1.0f;

    // Planar rigid-body state. The tank remains constrained to the terrain
    // surface, while its XZ velocity and yaw velocity carry momentum between
    // frames. A world-space velocity lets collision response preserve the
    // component tangent to an obstacle instead of losing all movement.
    glm::vec2 velocity_{0.0f};
    float angularVelocity_ = 0.0f;
    float movementAccumulator_ = 0.0f;
    float longitudinalAcceleration_ = 0.0f;
    float lateralSlipSpeed_ = 0.0f;

    float turretYaw_ = 0.0f;  // radians, relative to the hull, about local +Y
    float gunElevation_ = 0.0f;  // radians; positive raises the muzzle

    // Positive distance moves the barrel backward along its local -Z axis.
    // Firing changes this immediately; a damped spring returns it to rest.
    float barrelRecoilDistance_ = 0.0f;
    float barrelRecoilVelocity_ = 0.0f;

    float turretTurnSpeedRadians_ = 0.6f;    // rad/s, turret -- slower than hull for finer aiming

    glm::mat4 modelCorrection_{1.0f};

    // See prevHullWorldMatrix()/prevTurretWorldMatrix()/prevBarrelWorldMatrix().
    glm::mat4 prevHullMatrix_{1.0f};
    glm::mat4 prevTurretMatrix_{1.0f};
    glm::mat4 prevBarrelMatrix_{1.0f};
};
