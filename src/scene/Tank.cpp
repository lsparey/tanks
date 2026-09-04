#include "Tank.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "../io/ModelLoader.h"
#include "InputManager.h"
#include "Terrain.h"

namespace {

constexpr float kFixedMovementStep = 1.0f / 120.0f;
constexpr float kMaxFrameDelta = 0.1f;
constexpr float kForwardTopSpeed = 7.5f;
constexpr float kReverseTopSpeed = 4.5f;
constexpr float kEngineAcceleration = 7.5f;
constexpr float kBrakingAcceleration = 11.0f;
constexpr float kRollingResistance = 0.7f;
constexpr float kLongitudinalDrag = 0.055f;
constexpr float kLateralGrip = 9.0f;
constexpr float kAngularAcceleration = 5.5f;
constexpr float kAngularDrag = 3.5f;
constexpr float kPivotYawRate = 1.35f;
constexpr float kMovingYawRate = 0.82f;
constexpr float kSlopeGravityScale = 0.72f;
constexpr float kGravity = 9.81f;
constexpr float kMaximumGroundSpeed = 11.0f;
constexpr float kMinimumClimbNormalY = 0.7880108f;  // cos(38 degrees)
constexpr float kSuspensionContactInset = 0.42f;
constexpr float kSuspensionHeightFrequency = 5.0f;
constexpr float kSuspensionAngleFrequency = 4.0f;
constexpr float kSuspensionDampingRatio = 0.82f;
constexpr float kAccelerationPitchScale = 0.006f;
constexpr float kAccelerationRollScale = 0.006f;
constexpr float kMaximumDynamicPitch = 0.0610865f;  // 3.5 degrees
constexpr float kMaximumDynamicRoll = 0.0523599f;   // 3 degrees
constexpr float kTrackCenterOffsetScale = 0.39f;
constexpr float kGroundFeedbackResponse = 10.0f;
constexpr float kBarrelRecoilKickDistance = 0.28f;
constexpr float kMaximumBarrelRecoilDistance = 0.42f;
constexpr float kBarrelRecoilFrequency = 3.2f;
constexpr float kBarrelRecoilDampingRatio = 0.72f;
constexpr float kHullRecoilImpulseSpeed = 0.24f;
constexpr float kGunElevationSpeedRadians = 0.1745329f;  // 10 degrees/s
constexpr float kMinimumGunElevation = -0.1745329f;      // -10 degrees
constexpr float kMaximumGunElevation = 0.3490659f;       // +20 degrees

float moveTowards(float value, float target, float maxDelta) {
    if (value < target) return std::min(value + maxDelta, target);
    return std::max(value - maxDelta, target);
}

void springTowards(float& value, float& velocity, float target, float frequency,
                   float dampingRatio, float deltaTime) {
    constexpr float kTwoPi = 6.2831853f;
    float angularFrequency = kTwoPi * frequency;
    float acceleration = angularFrequency * angularFrequency * (target - value) -
                         2.0f * dampingRatio * angularFrequency * velocity;
    velocity += acceleration * deltaTime;
    value += velocity * deltaTime;
}

bool containsCaseInsensitive(const std::string& haystack, const char* needle) {
    std::string lower = haystack;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return lower.find(needle) != std::string::npos;
}

}  // namespace

Tank::Tank(VulkanContext& ctx, CommandContext& commands, const std::string& modelPath) {
    load(ctx, commands, modelPath);
}

void Tank::load(VulkanContext& ctx, CommandContext& commands, const std::string& path) {
    ModelLoader::Result result = ModelLoader::load(path);

    // The .x file's baked per-part material colors (tan hull/turret, sage
    // tracks, grey barrel) are overridden to near-white here rather than
    // touching ModelLoader (which is generic, not tank-specific) -- the tank
    // is drawn with an actual camo texture now (see Application's
    // camoMaterialSet_/CamoTextureGenerator), and letting the old baked
    // tints multiply against it would muddy its colors, the same reasoning
    // the crate/track/bark/leaf textures use their own near-white tint for.
    constexpr float kColorTint = 0.95f;
    for (auto& part : result.parts) {
        for (auto& vertex : part.vertices) vertex.color = glm::vec3(kColorTint);
    }

    // This old MilkShape export's UV unwrap is unusable for a tiled material
    // texture: most materials (Base/Detail/Tracks, i.e. everything that
    // becomes the hull below) leave every vertex at ModelLoader's (0,0)
    // fallback, sampling one flat texel instead of the intended camo/metal
    // pattern; the turret's own unwrap technically has real coordinates but
    // is a pinched/fan-shaped projection that reads as visibly distorted
    // (lines converging to a point) rather than a clean tiling. Discard the
    // model's UVs entirely and substitute one consistent projection for
    // every vertex: pick whichever local axis its normal points closest to
    // and project the other two position components onto it, a cheap
    // triplanar-style approximation -- good enough for a tileable material
    // texture with no sharp/aligned features to keep consistent across a
    // seam, and undistorted since it's a straight orthogonal projection
    // rather than the original's fan unwrap.
    constexpr float kSyntheticUVScale = 0.4f;
    for (auto& part : result.parts) {
        for (auto& vertex : part.vertices) {
            glm::vec3 n = glm::abs(vertex.normal);
            if (n.x >= n.y && n.x >= n.z) {
                vertex.uv = glm::vec2(vertex.position.y, vertex.position.z) * kSyntheticUVScale;
            } else if (n.y >= n.x && n.y >= n.z) {
                vertex.uv = glm::vec2(vertex.position.x, vertex.position.z) * kSyntheticUVScale;
            } else {
                vertex.uv = glm::vec2(vertex.position.x, vertex.position.y) * kSyntheticUVScale;
            }
        }
    }

    std::vector<Vertex> hullVertices;
    std::vector<uint32_t> hullIndices;
    ModelLoader::Part* turretPart = nullptr;
    ModelLoader::Part* barrelPart = nullptr;
    ModelLoader::Part* trackPart = nullptr;

    for (auto& part : result.parts) {
        if (containsCaseInsensitive(part.materialName, "turret")) {
            turretPart = &part;
        } else if (containsCaseInsensitive(part.materialName, "barrel")) {
            barrelPart = &part;
        } else if (containsCaseInsensitive(part.materialName, "track")) {
            trackPart = &part;
        } else {
            uint32_t base = static_cast<uint32_t>(hullVertices.size());
            hullVertices.insert(hullVertices.end(), part.vertices.begin(), part.vertices.end());
            for (uint32_t idx : part.indices) hullIndices.push_back(base + idx);
        }
    }

    // Local-space X extent of the hull, i.e. the tank's actual outer width
    // (left track's outer edge to the right track's outer edge) -- used by
    // Application to size TrackMark decals so they line up with the real
    // hull instead of a guessed constant. Local X is what basis(right_, ...)
    // maps to world "right" in hullWorldMatrix, same reasoning muzzleLocal_
    // above relies on for the barrel's local Z axis.
    float minX = hullVertices.empty() ? 0.0f : hullVertices[0].position.x;
    float maxX = minX;
    float minZ = hullVertices.empty() ? 0.0f : hullVertices[0].position.z;
    float maxZ = minZ;
    for (const auto& v : hullVertices) {
        minX = std::min(minX, v.position.x);
        maxX = std::max(maxX, v.position.x);
        minZ = std::min(minZ, v.position.z);
        maxZ = std::max(maxZ, v.position.z);
    }
    hullWidth_ = maxX - minX;
    hullLength_ = maxZ - minZ;

    hullMesh_ = std::make_unique<Mesh>(ctx, commands, hullVertices, hullIndices);
    hullBLAS_ = std::make_unique<AccelerationStructure>(AccelerationStructure::buildBLAS(ctx, commands, *hullMesh_));
    if (turretPart) {
        turretMesh_ = std::make_unique<Mesh>(ctx, commands, turretPart->vertices, turretPart->indices);
        turretBLAS_ =
            std::make_unique<AccelerationStructure>(AccelerationStructure::buildBLAS(ctx, commands, *turretMesh_));
    }
    if (barrelPart) {
        barrelMesh_ = std::make_unique<Mesh>(ctx, commands, barrelPart->vertices, barrelPart->indices);
        barrelBLAS_ =
            std::make_unique<AccelerationStructure>(AccelerationStructure::buildBLAS(ctx, commands, *barrelMesh_));

        // The muzzle tip is the barrel-part vertex farthest from the local
        // origin. A plain "farthest from the part's own centroid" heuristic
        // doesn't work for a barrel: a straight cylinder is roughly
        // symmetric along its length, so both the breech and muzzle ends
        // are equally far from ITS centroid. The local origin, however, is
        // the turret's pivot, which sits near the breech/mount end -- so
        // the point farthest from the origin is reliably the far (muzzle)
        // end instead.
        float maxDistSq = -1.0f;
        glm::vec3 barrelMinimum = barrelPart->vertices.front().position;
        glm::vec3 barrelMaximum = barrelMinimum;
        for (const auto& v : barrelPart->vertices) {
            float distSq = glm::dot(v.position, v.position);
            if (distSq > maxDistSq) {
                maxDistSq = distSq;
                muzzleLocal_ = v.position;
            }
            barrelMinimum = glm::min(barrelMinimum, v.position);
            barrelMaximum = glm::max(barrelMaximum, v.position);
        }

        // This model's barrel is authored along local +Z and begins at a
        // flat breech cross-section inside the turret. Its AABB center in
        // X/Y plus the minimum Z therefore gives a robust trunnion without
        // hardcoding a coordinate from this particular asset export.
        barrelPivotLocal_ = glm::vec3(0.5f * (barrelMinimum.x + barrelMaximum.x),
                                      0.5f * (barrelMinimum.y + barrelMaximum.y),
                                      barrelMinimum.z);
    }
    if (trackPart) {
        trackMesh_ = std::make_unique<Mesh>(ctx, commands, trackPart->vertices, trackPart->indices);
        trackBLAS_ =
            std::make_unique<AccelerationStructure>(AccelerationStructure::buildBLAS(ctx, commands, *trackMesh_));
    }
}

void Tank::update(const InputManager& input, float deltaTime, const Terrain& terrain,
                   const std::vector<CollisionSystem::CircleObstacle>& obstacles,
                   float boundaryHalfExtent) {
    float throttle = 0.0f;
    if (input.isKeyDown(GLFW_KEY_W)) throttle += 1.0f;
    if (input.isKeyDown(GLFW_KEY_S)) throttle -= 1.0f;

    float turn = 0.0f;
    if (input.isKeyDown(GLFW_KEY_A)) turn += 1.0f;
    if (input.isKeyDown(GLFW_KEY_D)) turn -= 1.0f;

    if (input.isKeyDown(GLFW_KEY_Q)) turretYaw_ += turretTurnSpeedRadians_ * deltaTime;
    if (input.isKeyDown(GLFW_KEY_E)) turretYaw_ -= turretTurnSpeedRadians_ * deltaTime;
    if (input.isKeyDown(GLFW_KEY_R)) gunElevation_ += kGunElevationSpeedRadians * deltaTime;
    if (input.isKeyDown(GLFW_KEY_F)) gunElevation_ -= kGunElevationSpeedRadians * deltaTime;
    gunElevation_ =
        glm::clamp(gunElevation_, kMinimumGunElevation, kMaximumGunElevation);

    updateGunRecoil(deltaTime);

    // Fixed simulation steps make force integration and collision response
    // independent of render cadence. Discard excessive time after a stall or
    // debugger pause rather than trying to simulate a huge, unstable jump.
    movementAccumulator_ += glm::clamp(deltaTime, 0.0f, kMaxFrameDelta);
    while (movementAccumulator_ >= kFixedMovementStep) {
        simulateMovement(throttle, turn, kFixedMovementStep, terrain, obstacles,
                         boundaryHalfExtent);
        movementAccumulator_ -= kFixedMovementStep;
    }

    // Ensure the initial pose is grounded even on a render frame too short
    // to produce its first fixed simulation step.
    updateGroundPose(terrain);
    if (!suspensionInitialized_) updateSuspensionPose(terrain, 0.0f, 0.0f, 0.0f);
}

void Tank::applyGunRecoil() {
    // Displace immediately so even a single rendered frame communicates
    // the shot, while repeated shots can accumulate a little without ever
    // pulling the barrel implausibly far into the turret.
    barrelRecoilDistance_ =
        std::min(barrelRecoilDistance_ + kBarrelRecoilKickDistance,
                 kMaximumBarrelRecoilDistance);
    barrelRecoilVelocity_ = std::max(barrelRecoilVelocity_, 0.0f);

    // Apply the equal-and-opposite response in the turret's firing direction,
    // projected onto the ground plane because the gameplay body is terrain
    // constrained. The existing traction and drag then settle the impulse.
    glm::vec3 shotDirection = aimDirection();
    glm::vec2 planarShotDirection(shotDirection.x, shotDirection.z);
    float planarLength = glm::length(planarShotDirection);
    if (planarLength > 1e-5f) {
        velocity_ -= (planarShotDirection / planarLength) * kHullRecoilImpulseSpeed;
    }
}

void Tank::updateGunRecoil(float deltaTime) {
    // Integrate in small steps so a window drag or debugger pause cannot
    // destabilize the relatively quick return spring.
    float remaining = glm::clamp(deltaTime, 0.0f, kMaxFrameDelta);
    while (remaining > 0.0f) {
        float step = std::min(remaining, kFixedMovementStep);
        springTowards(barrelRecoilDistance_, barrelRecoilVelocity_, 0.0f,
                      kBarrelRecoilFrequency, kBarrelRecoilDampingRatio, step);
        remaining -= step;
    }

    // The barrel should settle at its authored rest position rather than
    // visibly oscillating forward through it.
    if (barrelRecoilDistance_ <= 0.0f) {
        barrelRecoilDistance_ = 0.0f;
        barrelRecoilVelocity_ = 0.0f;
    }
}

void Tank::simulateMovement(
    float throttle, float turn, float deltaTime, const Terrain& terrain,
    const std::vector<CollisionSystem::CircleObstacle>& obstacles, float boundaryHalfExtent) {
    glm::vec2 velocityBeforeStep = velocity_;
    glm::vec2 flatForward(std::sin(yaw_), std::cos(yaw_));
    glm::vec2 flatRight(flatForward.y, -flatForward.x);

    // Interpret controls as independent track demands. Their average creates
    // longitudinal drive, while their difference creates turning torque.
    // This naturally permits pivot turns and reduces drive force in a hard
    // moving turn because the inside track slows or reverses.
    float leftTrack = glm::clamp(throttle - turn, -1.0f, 1.0f);
    float rightTrack = glm::clamp(throttle + turn, -1.0f, 1.0f);
    float driveDemand = 0.5f * (leftTrack + rightTrack);
    float turnDemand = 0.5f * (rightTrack - leftTrack);

    float forwardSpeed = glm::dot(velocity_, flatForward);
    float lateralSpeed = glm::dot(velocity_, flatRight);

    if (std::abs(driveDemand) > 1e-4f) {
        bool braking = forwardSpeed * driveDemand < -0.05f;
        float acceleration = kBrakingAcceleration;
        if (!braking) {
            float topSpeed = driveDemand > 0.0f ? kForwardTopSpeed : kReverseTopSpeed;
            float speedAlongDemand = forwardSpeed * (driveDemand > 0.0f ? 1.0f : -1.0f);
            float motorFactor = glm::clamp(1.0f - speedAlongDemand / topSpeed, 0.0f, 1.0f);
            acceleration = kEngineAcceleration * motorFactor;
        }
        forwardSpeed += driveDemand * acceleration * deltaTime;
    } else {
        forwardSpeed = moveTowards(forwardSpeed, 0.0f, kRollingResistance * deltaTime);
    }

    // Air/drive-train drag grows with speed, while strong lateral track grip
    // quickly scrubs sideways motion without making it disappear instantly.
    forwardSpeed *= std::exp(-kLongitudinalDrag * std::abs(forwardSpeed) * deltaTime);
    lateralSpeed *= std::exp(-kLateralGrip * deltaTime);
    velocity_ = flatForward * forwardSpeed + flatRight * lateralSpeed;

    glm::vec3 groundNormal = terrain.normalAt(position_.x, position_.z);
    glm::vec3 gravity(0.0f, -kGravity, 0.0f);
    glm::vec3 slopeGravity = gravity - groundNormal * glm::dot(gravity, groundNormal);
    velocity_ += glm::vec2(slopeGravity.x, slopeGravity.z) *
                 (kSlopeGravityScale * deltaTime);

    float speedRatio = glm::clamp(std::abs(forwardSpeed) / kForwardTopSpeed, 0.0f, 1.0f);
    float yawLimit = glm::mix(kPivotYawRate, kMovingYawRate, speedRatio);
    float turnAuthority = glm::mix(1.0f, 0.65f, speedRatio);
    angularVelocity_ += turnDemand * kAngularAcceleration * turnAuthority * deltaTime;
    angularVelocity_ *= std::exp(-kAngularDrag * deltaTime);
    angularVelocity_ = glm::clamp(angularVelocity_, -yawLimit, yawLimit);

    yaw_ += angularVelocity_ * deltaTime;
    position_.x += velocity_.x * deltaTime;
    position_.z += velocity_.y * deltaTime;

    // Hull-width-derived collision radius (a circle is a rough stand-in for
    // the hull's actual rectangular footprint, but is enough to stop the
    // tank driving through a tree/rock without needing real hull-vs-hull
    // shape collision).
    float collisionRadius = hullWidth_ * 0.6f;
    CollisionSystem::CircleCollisionResult collision =
        CollisionSystem::resolveCircleCollisions(glm::vec2(position_.x, position_.z), velocity_,
                                                  collisionRadius, obstacles);
    position_.x = collision.position.x;
    position_.z = collision.position.y;
    velocity_ = collision.velocity;

    // Keep the hull fully inside the play-area boundary (see
    // BoundaryGenerator) rather than letting it drive out through the wall
    // of light -- clamped by the hull's own collision radius so the wall
    // reads as a solid surface the tank's front stops flush against,
    // rather than the hull's center (and therefore half its body) crossing
    // through before the clamp takes effect.
    float clampExtent = boundaryHalfExtent - collisionRadius;
    if (position_.x < -clampExtent) {
        position_.x = -clampExtent;
        if (velocity_.x < 0.0f) velocity_.x = 0.0f;
    } else if (position_.x > clampExtent) {
        position_.x = clampExtent;
        if (velocity_.x > 0.0f) velocity_.x = 0.0f;
    }
    if (position_.z < -clampExtent) {
        position_.z = -clampExtent;
        if (velocity_.y < 0.0f) velocity_.y = 0.0f;
    } else if (position_.z > clampExtent) {
        position_.z = clampExtent;
        if (velocity_.y > 0.0f) velocity_.y = 0.0f;
    }

    groundNormal = terrain.normalAt(position_.x, position_.z);
    if (groundNormal.y < kMinimumClimbNormalY) {
        glm::vec2 uphill(-groundNormal.x, -groundNormal.z);
        float uphillLength = glm::length(uphill);
        if (uphillLength > 1e-5f) {
            uphill /= uphillLength;
            float uphillSpeed = glm::dot(velocity_, uphill);
            if (uphillSpeed > 0.0f) velocity_ -= uphill * uphillSpeed;
        }
    }

    float groundSpeed = glm::length(velocity_);
    if (groundSpeed > kMaximumGroundSpeed) {
        velocity_ *= kMaximumGroundSpeed / groundSpeed;
    }

    updateGroundPose(terrain);

    glm::vec2 acceleration = (velocity_ - velocityBeforeStep) / deltaTime;
    glm::vec2 currentForward(std::sin(yaw_), std::cos(yaw_));
    glm::vec2 currentRight(currentForward.y, -currentForward.x);
    float longitudinalAcceleration = glm::dot(acceleration, currentForward);
    float feedbackBlend = 1.0f - std::exp(-kGroundFeedbackResponse * deltaTime);
    longitudinalAcceleration_ =
        glm::mix(longitudinalAcceleration_, longitudinalAcceleration, feedbackBlend);
    lateralSlipSpeed_ = glm::mix(lateralSlipSpeed_, std::abs(glm::dot(velocity_, currentRight)),
                                 feedbackBlend);
    updateSuspensionPose(terrain, deltaTime, longitudinalAcceleration,
                         glm::dot(acceleration, currentRight));
}

void Tank::updateGroundPose(const Terrain& terrain) {
    glm::vec3 flatForward(std::sin(yaw_), 0.0f, std::cos(yaw_));

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

void Tank::updateSuspensionPose(const Terrain& terrain, float deltaTime,
                                float longitudinalAcceleration, float lateralAcceleration) {
    glm::vec2 flatForward(std::sin(yaw_), std::cos(yaw_));
    glm::vec2 flatRight(flatForward.y, -flatForward.x);
    float halfLength = std::max(hullLength_ * kSuspensionContactInset, 0.5f);
    float halfWidth = std::max(hullWidth_ * kSuspensionContactInset, 0.35f);

    auto contactHeight = [&](float forwardOffset, float rightOffset) {
        glm::vec2 xz(position_.x, position_.z);
        xz += flatForward * forwardOffset + flatRight * rightOffset;
        return terrain.heightAt(xz.x, xz.y);
    };

    float frontLeft = contactHeight(halfLength, -halfWidth);
    float frontRight = contactHeight(halfLength, halfWidth);
    float rearLeft = contactHeight(-halfLength, -halfWidth);
    float rearRight = contactHeight(-halfLength, halfWidth);

    float frontHeight = 0.5f * (frontLeft + frontRight);
    float rearHeight = 0.5f * (rearLeft + rearRight);
    float leftHeight = 0.5f * (frontLeft + rearLeft);
    float rightHeight = 0.5f * (frontRight + rearRight);
    float targetHeight = 0.25f * (frontLeft + frontRight + rearLeft + rearRight);

    float terrainPitch = std::atan2(frontHeight - rearHeight, 2.0f * halfLength);
    float terrainRoll = std::atan2(rightHeight - leftHeight, 2.0f * halfWidth);
    float dynamicPitch = glm::clamp(longitudinalAcceleration * kAccelerationPitchScale,
                                    -kMaximumDynamicPitch, kMaximumDynamicPitch);
    float dynamicRoll = glm::clamp(lateralAcceleration * kAccelerationRollScale,
                                   -kMaximumDynamicRoll, kMaximumDynamicRoll);
    float targetPitch = terrainPitch + dynamicPitch;
    float targetRoll = terrainRoll + dynamicRoll;

    if (!suspensionInitialized_) {
        suspensionHeight_ = targetHeight;
        suspensionPitch_ = targetPitch;
        suspensionRoll_ = targetRoll;
        suspensionInitialized_ = true;
    } else {
        springTowards(suspensionHeight_, suspensionHeightVelocity_, targetHeight,
                      kSuspensionHeightFrequency, kSuspensionDampingRatio, deltaTime);
        springTowards(suspensionPitch_, suspensionPitchVelocity_, targetPitch,
                      kSuspensionAngleFrequency, kSuspensionDampingRatio, deltaTime);
        springTowards(suspensionRoll_, suspensionRollVelocity_, targetRoll,
                      kSuspensionAngleFrequency, kSuspensionDampingRatio, deltaTime);
    }

    visualPosition_ = glm::vec3(position_.x, suspensionHeight_, position_.z);
    glm::vec3 horizontalForward(flatForward.x, 0.0f, flatForward.y);
    glm::vec3 horizontalRight(flatRight.x, 0.0f, flatRight.y);
    visualForward_ =
        glm::normalize(horizontalForward + glm::vec3(0.0f, std::tan(suspensionPitch_), 0.0f));
    glm::vec3 rolledRight =
        glm::normalize(horizontalRight + glm::vec3(0.0f, std::tan(suspensionRoll_), 0.0f));
    visualUp_ = glm::normalize(glm::cross(visualForward_, rolledRight));
    visualRight_ = glm::normalize(glm::cross(visualUp_, visualForward_));

    auto contactAmount = [&](float side) {
        glm::vec3 visualContact =
            visualPosition_ + visualRight_ * (side * hullWidth_ * kTrackCenterOffsetScale);
        float groundHeight = terrain.heightAt(visualContact.x, visualContact.z);
        float gap = visualContact.y - groundHeight;
        return 1.0f - glm::smoothstep(0.08f, 0.28f, gap);
    };
    leftTrackContactAmount_ = contactAmount(-1.0f);
    rightTrackContactAmount_ = contactAmount(1.0f);
}

glm::vec3 Tank::leftTrackGroundPosition() const {
    glm::vec2 flatRight(std::cos(yaw_), -std::sin(yaw_));
    glm::vec2 xz(position_.x, position_.z);
    xz -= flatRight * (hullWidth_ * kTrackCenterOffsetScale);
    return glm::vec3(xz.x, position_.y, xz.y);
}

glm::vec3 Tank::rightTrackGroundPosition() const {
    glm::vec2 flatRight(std::cos(yaw_), -std::sin(yaw_));
    glm::vec2 xz(position_.x, position_.z);
    xz += flatRight * (hullWidth_ * kTrackCenterOffsetScale);
    return glm::vec3(xz.x, position_.y, xz.y);
}

float Tank::leftTrackGroundSpeed() const {
    glm::vec2 flatForward(std::sin(yaw_), std::cos(yaw_));
    glm::vec2 contactVelocity =
        velocity_ + flatForward * (angularVelocity_ * hullWidth_ * kTrackCenterOffsetScale);
    return glm::length(contactVelocity);
}

float Tank::rightTrackGroundSpeed() const {
    glm::vec2 flatForward(std::sin(yaw_), std::cos(yaw_));
    glm::vec2 contactVelocity =
        velocity_ - flatForward * (angularVelocity_ * hullWidth_ * kTrackCenterOffsetScale);
    return glm::length(contactVelocity);
}

glm::mat4 Tank::hullWorldMatrix() const {
    glm::mat4 basis(glm::vec4(visualRight_, 0.0f), glm::vec4(visualUp_, 0.0f),
                    glm::vec4(visualForward_, 0.0f), glm::vec4(visualPosition_, 1.0f));
    return basis * modelCorrection_;
}

glm::mat4 Tank::turretWorldMatrix() const {
    // Rotate about the model's own local +Y axis (no separate pivot offset
    // -- matches the original DirectX project's approach for this exact
    // asset, which works because the turret geometry happens to be authored
    // so its natural rotation center coincides with the model origin), then
    // carry it along with the hull's full placement.
    glm::mat4 localSpin = glm::rotate(glm::mat4(1.0f), turretYaw_, glm::vec3(0.0f, 1.0f, 0.0f));
    return hullWorldMatrix() * localSpin;
}

glm::mat4 Tank::barrelWorldMatrix() const {
    // GLM's positive rotation around local +X pitches +Z downward, hence
    // the negative elevation angle here. Translate to/from the derived
    // trunnion so the breech stays seated in the mantlet. Recoil is applied
    // in barrel-local space before that pitch, keeping it on the bore axis.
    glm::mat4 toPivot = glm::translate(glm::mat4(1.0f), barrelPivotLocal_);
    glm::mat4 elevation =
        glm::rotate(glm::mat4(1.0f), -gunElevation_, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 fromPivot = glm::translate(glm::mat4(1.0f), -barrelPivotLocal_);
    glm::mat4 recoil = glm::translate(glm::mat4(1.0f),
                                      glm::vec3(0.0f, 0.0f, -barrelRecoilDistance_));
    return turretWorldMatrix() * toPivot * elevation * fromPivot * recoil;
}

std::vector<Tank::DrawPart> Tank::drawParts() const {
    std::vector<DrawPart> parts;
    parts.push_back({hullMesh_.get(), hullWorldMatrix(), hullBLAS_->deviceAddress()});
    // Tracks share the hull's own placement (they don't move independently
    // in this prototype) but are bare metal rather than painted camo.
    if (trackMesh_) {
        parts.push_back({trackMesh_.get(), hullWorldMatrix(), trackBLAS_->deviceAddress(), /*metallic=*/true});
    }

    if (turretMesh_ || barrelMesh_) {
        glm::mat4 turretWorld = turretWorldMatrix();
        if (turretMesh_) {
            parts.push_back({turretMesh_.get(), turretWorld, turretBLAS_->deviceAddress()});
        }
        // The barrel isn't independently elevated yet -- it just rides
        // along with the turret's yaw, with a local translation added for
        // firing recoil. Bare metal, like the tracks.
        if (barrelMesh_) {
            parts.push_back(
                {barrelMesh_.get(), barrelWorldMatrix(), barrelBLAS_->deviceAddress(),
                 /*metallic=*/true});
        }
    }
    return parts;
}

glm::vec3 Tank::aimDirection() const {
    if (!turretMesh_ && !barrelMesh_) return visualForward_;

    if (barrelMesh_) {
        // Translation components (pivot and recoil) disappear for a vector
        // with w=0, leaving exactly the yawed/elevated bore direction used
        // by the rendered barrel.
        return glm::normalize(
            glm::vec3(barrelWorldMatrix() * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
    }

    // A turret-only fallback has yaw but no independently pitched geometry.
    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), turretYaw_, visualUp_);
    return glm::normalize(glm::vec3(rot * glm::vec4(visualForward_, 0.0f)));
}

glm::vec3 Tank::muzzleWorldPosition() const {
    if (barrelMesh_) {
        return glm::vec3(barrelWorldMatrix() * glm::vec4(muzzleLocal_, 1.0f));
    }
    // No separate barrel material in the source model -- approximate.
    return visualPosition_ + aimDirection() * 3.3f + visualUp_ * 1.3f;
}
