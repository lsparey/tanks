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
    for (const auto& v : hullVertices) {
        minX = std::min(minX, v.position.x);
        maxX = std::max(maxX, v.position.x);
    }
    hullWidth_ = maxX - minX;

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
        for (const auto& v : barrelPart->vertices) {
            float distSq = glm::dot(v.position, v.position);
            if (distSq > maxDistSq) {
                maxDistSq = distSq;
                muzzleLocal_ = v.position;
            }
        }
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

    yaw_ += turn * turnSpeedRadians_ * deltaTime;

    if (input.isKeyDown(GLFW_KEY_Q)) turretYaw_ += turretTurnSpeedRadians_ * deltaTime;
    if (input.isKeyDown(GLFW_KEY_E)) turretYaw_ -= turretTurnSpeedRadians_ * deltaTime;

    glm::vec3 flatForward(std::sin(yaw_), 0.0f, std::cos(yaw_));
    position_ += flatForward * (throttle * moveSpeed_ * deltaTime);

    // Hull-width-derived collision radius (a circle is a rough stand-in for
    // the hull's actual rectangular footprint, but is enough to stop the
    // tank driving through a tree/rock without needing real hull-vs-hull
    // shape collision).
    float collisionRadius = hullWidth_ * 0.6f;
    glm::vec2 resolvedXZ = CollisionSystem::resolveCircleCollisions(
        glm::vec2(position_.x, position_.z), collisionRadius, obstacles);
    position_.x = resolvedXZ.x;
    position_.z = resolvedXZ.y;

    // Keep the hull fully inside the play-area boundary (see
    // BoundaryGenerator) rather than letting it drive out through the wall
    // of light -- clamped by the hull's own collision radius so the wall
    // reads as a solid surface the tank's front stops flush against,
    // rather than the hull's center (and therefore half its body) crossing
    // through before the clamp takes effect.
    float clampExtent = boundaryHalfExtent - collisionRadius;
    position_.x = glm::clamp(position_.x, -clampExtent, clampExtent);
    position_.z = glm::clamp(position_.z, -clampExtent, clampExtent);

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

glm::mat4 Tank::hullWorldMatrix() const {
    glm::mat4 basis(glm::vec4(right_, 0.0f), glm::vec4(up_, 0.0f), glm::vec4(forward_, 0.0f),
                     glm::vec4(position_, 1.0f));
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
        // along with the turret's yaw, so it shares the same matrix. Bare
        // metal, like the tracks.
        if (barrelMesh_) {
            parts.push_back(
                {barrelMesh_.get(), turretWorld, barrelBLAS_->deviceAddress(), /*metallic=*/true});
        }
    }
    return parts;
}

glm::vec3 Tank::aimDirection() const {
    if (!turretMesh_ && !barrelMesh_) return forward_;
    // Rotating the world-space forward_ around the world-space up_ by
    // turretYaw_ is equivalent to rotating the local forward direction
    // around local Y and then applying the hull's (rigid, orthonormal)
    // transform -- same result as turretWorldMatrix(), without needing to
    // go through local space for a plain direction vector.
    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), turretYaw_, up_);
    return glm::normalize(glm::vec3(rot * glm::vec4(forward_, 0.0f)));
}

glm::vec3 Tank::muzzleWorldPosition() const {
    if (barrelMesh_) {
        return glm::vec3(turretWorldMatrix() * glm::vec4(muzzleLocal_, 1.0f));
    }
    // No separate barrel material in the source model -- approximate.
    return position_ + aimDirection() * 3.3f + glm::vec3(0.0f, 1.3f, 0.0f);
}
