#include "MatchState.h"

#include <algorithm>

MatchState::MatchState(float playerFuelCapacity, float opponentFuelCapacity) {
    auto& player = combatants_[static_cast<size_t>(CombatantId::Player)];
    player.fuelRemaining = player.fuelCapacity = playerFuelCapacity;
    auto& opponent = combatants_[static_cast<size_t>(CombatantId::Opponent)];
    opponent.fuelRemaining = opponent.fuelCapacity = opponentFuelCapacity;
}

std::optional<CombatantId> MatchState::winner() const {
    if (phase_ != Phase::GameOver) return std::nullopt;
    if (combatants_[static_cast<size_t>(CombatantId::Player)].alive) return CombatantId::Player;
    if (combatants_[static_cast<size_t>(CombatantId::Opponent)].alive) return CombatantId::Opponent;
    return std::nullopt;
}

void MatchState::spendFuel(float amount) {
    if (phase_ != Phase::Move) return;
    auto& active = combatants_[static_cast<size_t>(active_)];
    active.fuelRemaining = std::max(0.0f, active.fuelRemaining - std::max(0.0f, amount));
    if (active.fuelRemaining <= 0.0f) endMovePhase();
}

void MatchState::endMovePhase() {
    if (phase_ != Phase::Move) return;
    phase_ = Phase::AimFire;
}

void MatchState::recordShotFired() {
    if (phase_ != Phase::AimFire) return;
    auto& active = combatants_[static_cast<size_t>(active_)];
    if (active.shellsRemaining <= 0) return;
    --active.shellsRemaining;
    phase_ = Phase::Resolving;
}

void MatchState::applyDamage(CombatantId target, float damageInFullHits) {
    if (phase_ != Phase::Resolving || damageInFullHits <= 0.0f) return;
    auto& victim = combatants_[static_cast<size_t>(target)];
    if (!victim.alive) return;
    victim.health = std::max(0.0f, victim.health - damageInFullHits);
    if (victim.health <= 0.0f) {
        victim.alive = false;
        phase_ = Phase::GameOver;
    }
}

void MatchState::notifyProjectilesSettled() {
    if (phase_ != Phase::Resolving) return;
    if (combatants_[static_cast<size_t>(active_)].shellsRemaining > 0) phase_ = Phase::AimFire;
    else endTurn();
}

void MatchState::endTurn() {
    active_ = active_ == CombatantId::Player ? CombatantId::Opponent : CombatantId::Player;
    auto& next = combatants_[static_cast<size_t>(active_)];
    next.fuelRemaining = next.fuelCapacity;
    next.shellsRemaining = next.shellsPerTurn;
    phase_ = Phase::Move;
}

void MatchState::collectPowerUp(CombatantId id, PowerUpType type) {
    auto& c = combatants_[static_cast<size_t>(id)];
    switch (type) {
        case PowerUpType::MoreFuel:
            c.fuelCapacity += 10.0f;
            break;
        case PowerUpType::TighterAccuracy:
            c.accuracyBonus += 0.5f;
            break;
        default:
            ++c.powerUps[static_cast<size_t>(type)];
            break;
    }
}

void MatchState::armPowerUp(PowerUpType type) {
    auto& active = combatants_[static_cast<size_t>(active_)];
    if (active.powerUps[static_cast<size_t>(type)] > 0) active.armedPowerUp = type;
}

void MatchState::consumeArmedPowerUp() {
    auto& active = combatants_[static_cast<size_t>(active_)];
    active.damageMultiplier = 1.0f;
    active.splashRadiusMultiplier = 1.0f;
    if (!active.armedPowerUp) return;
    PowerUpType type = *active.armedPowerUp;
    active.armedPowerUp.reset();
    int& count = active.powerUps[static_cast<size_t>(type)];
    if (count <= 0) return;
    --count;
    switch (type) {
        case PowerUpType::ExtraShell:
            ++active.shellsRemaining;
            break;
        case PowerUpType::IncreasedDamage:
            active.damageMultiplier = 1.5f;
            break;
        case PowerUpType::LargerSplash:
            active.splashRadiusMultiplier = 1.5f;
            break;
        case PowerUpType::AimAssist:
        case PowerUpType::TighterAccuracy:
        case PowerUpType::MoreFuel:
            break;
    }
}
