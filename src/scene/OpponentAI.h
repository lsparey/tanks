#pragma once

#include <span>

#include <glm/glm.hpp>

// Pure, deterministic opponent decision-making (see PLAN.md's "Opponent
// AI"). No Tank/Vulkan dependency, by design -- Tank needs a live Vulkan
// context to construct at all, so anything meant to be unit-tested
// headlessly has to work with plain positions/distances instead, the same
// split MatchState/Projectile already use. Application is the only thing
// that reads live Tank/MatchState data and turns these decisions into
// per-frame Tank::Controls.
namespace OpponentAI {

// Move-phase target: the nearest alive crate within `seekRadius` if any
// (PLAN.md's "reaching the nearest reachable crate"); otherwise a point
// `engagementRange` from the player's current position along the
// opponent-to-player line, closing distance -- or the opponent's own
// current position (hold, meaning "don't move") if already within
// `engagementRange` ("keeping line of fire": read here as not driving
// past a sensible engagement distance, not a line-of-sight raycast).
glm::vec2 chooseMoveTarget(glm::vec2 myPosition, glm::vec2 playerPosition,
                            std::span<const glm::vec2> aliveCrates,
                            float seekRadius = 45.0f, float engagementRange = 40.0f);

// Ballistic solve for a level shot -- this game's design already commits
// range to shot power, not elevation (see the Gameplay section's design
// decisions and item 4's measured landing-distance table), so this solves
// only power, from the same closed-form level-shot relationship that table
// is built on: requiredSpeed = horizontalDistance / sqrt(2*muzzleHeight/gravity).
// Returns a shot-power fraction in [0, 1] -- clamped, not extrapolated, so a
// target outside the reachable range gets the nearest achievable power
// rather than an impossible one.
float solvePowerForDistance(float horizontalDistance, float muzzleHeight, float gravity,
                            float minShotSpeed, float maxShotSpeed);

// Deliberate aim error in radians: shrinks as `difficulty` increases (1.0
// is the default) and as `accuracyBonus` (the same CombatantState field
// TighterAccuracy raises for the player's own dispersion, see
// MatchState::dispersionDegrees) increases. Floored at zero.
float aimErrorRadians(float difficulty, float accuracyBonus);

}  // namespace OpponentAI
