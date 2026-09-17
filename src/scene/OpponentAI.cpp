#include "OpponentAI.h"

#include <algorithm>
#include <cmath>

namespace OpponentAI {

glm::vec2 chooseMoveTarget(glm::vec2 myPosition, glm::vec2 playerPosition,
                            std::span<const glm::vec2> aliveCrates,
                            float seekRadius, float engagementRange) {
    const glm::vec2* nearestCrate = nullptr;
    float nearestCrateDistanceSq = seekRadius * seekRadius;
    for (const auto& crate : aliveCrates) {
        glm::vec2 delta = crate - myPosition;
        float distanceSq = glm::dot(delta, delta);
        if (distanceSq < nearestCrateDistanceSq) {
            nearestCrateDistanceSq = distanceSq;
            nearestCrate = &crate;
        }
    }
    if (nearestCrate) return *nearestCrate;

    glm::vec2 toPlayer = playerPosition - myPosition;
    float distance = glm::length(toPlayer);
    if (distance <= engagementRange) return myPosition;  // already in range: hold
    glm::vec2 direction = distance > 1e-4f ? toPlayer / distance : glm::vec2(0.0f, 1.0f);
    return playerPosition - direction * engagementRange;
}

float solvePowerForDistance(float horizontalDistance, float muzzleHeight, float gravity,
                            float minShotSpeed, float maxShotSpeed) {
    float fallTime = std::sqrt(std::max(0.0f, 2.0f * muzzleHeight / std::max(gravity, 1e-4f)));
    float requiredSpeed = fallTime > 1e-4f ? horizontalDistance / fallTime : maxShotSpeed;
    float range = std::max(maxShotSpeed - minShotSpeed, 1e-4f);
    return std::clamp((requiredSpeed - minShotSpeed) / range, 0.0f, 1.0f);
}

float aimErrorRadians(float difficulty, float accuracyBonus) {
    constexpr float kBaseAiErrorDegrees = 6.0f;  // at difficulty 1.0, accuracyBonus 0
    constexpr float kAccuracyBonusScale = 4.0f;  // degrees removed per accuracyBonus unit
    float degrees = kBaseAiErrorDegrees / std::max(difficulty, 0.1f) - accuracyBonus * kAccuracyBonusScale;
    return glm::radians(std::max(0.0f, degrees));
}

}  // namespace OpponentAI
