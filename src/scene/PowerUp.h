#pragma once

#include <array>
#include <cstddef>

// Power-ups granted by collecting a crate (see PLAN.md's "Power-up
// crates"). IncreasedDamage/LargerSplash/AimAssist are held in
// MatchState::CombatantState::powerUps until armed and consumed (see
// MatchState::armPowerUp/consumeArmedPowerUp); ExtraShell/TighterAccuracy/
// MoreFuel apply immediately on collection instead (see
// MatchState::collectPowerUp) -- there's no "this shot" meaning for a
// passive stat boost or an extra round in the rack to wait on.
//
// Lives in its own header (rather than MatchState.h) so a Box can carry the
// type it holds without the scene/render side depending on the whole rules
// engine.
enum class PowerUpType { ExtraShell, IncreasedDamage, LargerSplash, AimAssist, TighterAccuracy, MoreFuel };
inline constexpr size_t kPowerUpTypeCount = 6;
inline constexpr std::array<PowerUpType, kPowerUpTypeCount> kAllPowerUpTypes = {
    PowerUpType::ExtraShell,  PowerUpType::IncreasedDamage, PowerUpType::LargerSplash,
    PowerUpType::AimAssist,   PowerUpType::TighterAccuracy, PowerUpType::MoreFuel,
};
