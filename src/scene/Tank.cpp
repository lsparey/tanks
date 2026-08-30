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

    std::vector<Vertex> hullVertices;
    std::vector<uint32_t> hullIndices;
    ModelLoader::Part* turretPart = nullptr;
    ModelLoader::Part* barrelPart = nullptr;

    for (auto& part : result.parts) {
        if (containsCaseInsensitive(part.materialName, "turret")) {
            turretPart = &part;
        } else if (containsCaseInsensitive(part.materialName, "barrel")) {
            barrelPart = &part;
        } else {
            uint32_t base = static_cast<uint32_t>(hullVertices.size());
            hullVertices.insert(hullVertices.end(), part.vertices.begin(), part.vertices.end());
            for (uint32_t idx : part.indices) hullIndices.push_back(base + idx);
        }
    }

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
}

void Tank::update(const InputManager& input, float deltaTime, const Terrain& terrain) {
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

    if (turretMesh_ || barrelMesh_) {
        glm::mat4 turretWorld = turretWorldMatrix();
        if (turretMesh_) {
            parts.push_back({turretMesh_.get(), turretWorld, turretBLAS_->deviceAddress()});
        }
        // The barrel isn't independently elevated yet -- it just rides
        // along with the turret's yaw, so it shares the same matrix.
        if (barrelMesh_) {
            parts.push_back({barrelMesh_.get(), turretWorld, barrelBLAS_->deviceAddress()});
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
