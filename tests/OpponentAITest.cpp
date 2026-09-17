#include "scene/OpponentAI.h"

#include <stdexcept>
#include <vector>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(glm::vec2 a, glm::vec2 b) { return glm::length(a - b) < 1e-4f; }

int main() {
    // chooseMoveTarget: no crates, far from the player -> close distance to
    // exactly engagementRange along the opponent-to-player line.
    {
        glm::vec2 target = OpponentAI::chooseMoveTarget({0, 0}, {100, 0}, {}, 45.0f, 40.0f);
        require(close(target, {60, 0}), "Closes distance to exactly engagementRange from the player");
    }
    // Already within engagementRange, no crates -> hold (target == self).
    {
        glm::vec2 target = OpponentAI::chooseMoveTarget({0, 0}, {30, 0}, {}, 45.0f, 40.0f);
        require(close(target, {0, 0}), "Holds position once already within engagementRange");
    }
    // A crate within seekRadius takes priority over closing distance, even
    // though the player is far away.
    {
        std::vector<glm::vec2> crates = {{10, 0}, {-5, 5}};
        glm::vec2 target = OpponentAI::chooseMoveTarget({0, 0}, {200, 0}, crates, 45.0f, 40.0f);
        require(close(target, {-5, 5}), "Nearest in-range crate is prioritized over closing distance");
    }
    // A crate exists but is outside seekRadius -- ignored, falls back to
    // closing distance toward the player instead.
    {
        std::vector<glm::vec2> crates = {{100, 100}};
        glm::vec2 target = OpponentAI::chooseMoveTarget({0, 0}, {50, 0}, crates, 20.0f, 40.0f);
        require(close(target, {10, 0}), "Out-of-range crate is ignored, falls back to closing distance");
    }

    // solvePowerForDistance: monotonic in distance, clamped at both ends --
    // mirrors item 4's own landing-distance monotonicity tests.
    {
        constexpr float kMuzzleHeight = 1.5f, kGravity = 0.3f, kMinSpeed = 4.5f, kMaxSpeed = 45.0f;
        float previous = -1.0f;
        for (float distance : {5.0f, 20.0f, 50.0f, 90.0f, 140.0f}) {
            float power = OpponentAI::solvePowerForDistance(distance, kMuzzleHeight, kGravity, kMinSpeed, kMaxSpeed);
            require(power >= 0.0f && power <= 1.0f, "Solved power stays within [0, 1]");
            require(power > previous, "Solved power is strictly monotonic in distance");
            previous = power;
        }
        require(OpponentAI::solvePowerForDistance(0.0f, kMuzzleHeight, kGravity, kMinSpeed, kMaxSpeed) == 0.0f,
                "A target at zero distance clamps to minimum power");
        require(OpponentAI::solvePowerForDistance(10000.0f, kMuzzleHeight, kGravity, kMinSpeed, kMaxSpeed) == 1.0f,
                "A target far beyond the reachable range clamps to maximum power");
    }

    // aimErrorRadians: shrinks with difficulty and accuracyBonus, floored at zero.
    {
        float baseline = OpponentAI::aimErrorRadians(1.0f, 0.0f);
        require(baseline > 0.0f, "Default difficulty/accuracy still has some aim error");
        require(OpponentAI::aimErrorRadians(2.0f, 0.0f) < baseline, "Higher difficulty shrinks aim error");
        require(OpponentAI::aimErrorRadians(1.0f, 1.0f) < baseline, "Collected accuracy bonus shrinks aim error");
        require(OpponentAI::aimErrorRadians(1.0f, 100.0f) == 0.0f, "Aim error cannot go negative");
    }
}
