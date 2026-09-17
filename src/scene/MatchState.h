#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "PowerUp.h"

// The two combatants in a duel. Player always starts the match; see
// MatchState's constructor.
enum class CombatantId { Player, Opponent };

// The state machine a turn moves through. Turn covers the whole active
// combatant's turn -- driving (while fuel remains) and firing (while a
// shell remains) are both always available, in any order, driven by
// player/opponent input (see MatchState::spendFuel/recordShotFired); there
// is no separate "must stop moving before you may fire" step. Resolving is
// driven by whoever owns projectile simulation, once per shell's damage
// application and once when every shell fired this turn has landed and its
// effects have settled (see MatchState::applyDamage/notifyProjectilesSettled).
// GameOver is terminal.
enum class Phase { Turn, Resolving, GameOver };

// Owns the two combatants, whose turn it is, the phase within the turn and
// the win condition -- the single source of truth the fixed-step
// simulation, input handling and rendering read from without deciding
// rules themselves (see PLAN.md's "Match rules and turn state"). Every
// mutator below is a no-op, leaving all observable state unchanged, when
// called outside the phase it's valid in or after the match is over.
//
// MatchState does not know how many shells are currently in flight; that
// bookkeeping stays with whoever simulates projectiles. It only reacts to
// two events that bookkeeping produces: a hit landing (applyDamage) and
// the turn's shells being fully settled (notifyProjectilesSettled).
class MatchState {
public:
    // "Three solid hits destroy it" (PLAN.md): three full-damage hits, so
    // health is tracked in units of "full hits," not integer hit points --
    // splash hits contribute fractions of this that must be able to add up
    // across turns.
    static constexpr float kMaxHealth = 3.0f;

    // Fuel cost for one simulation step of tank movement -- distance
    // traveled (meters, at kFuelPerMeter fuel/meter) plus pivot rotation
    // (radians, at kFuelPerRadian fuel/radian). Pure and deterministic so a
    // scripted drive's cumulative fuel drain is exactly reproducible. Takes
    // plain floats rather than a Tank reference, keeping MatchState
    // decoupled from Tank exactly as it is everywhere else. Tuned by feel
    // for a first pass (a full 2*pi in-place pivot costs ~6.3 fuel out of
    // the default kDefaultFuelCapacity tank) -- revisit as real matches are
    // played; the first such revision raised the tank from 20 to 26 after
    // play showed turns running dry ~30% too early.
    static constexpr float kDefaultFuelCapacity = 26.0f;
    static constexpr float kFuelPerMeter = 1.0f;
    static constexpr float kFuelPerRadian = 1.0f;
    static float movementFuelCost(float forwardSpeed, float angularSpeed, float deltaTime) {
        return (std::abs(forwardSpeed) * kFuelPerMeter + std::abs(angularSpeed) * kFuelPerRadian) * deltaTime;
    }

    // Damage from a hit at `distance` from the hull surface (see
    // Tank::distanceToHull), within a splashRadius: 1.0 (a full hit) at
    // distance 0 -- which is exactly what a direct hit is, distanceToHull's
    // own zero case, so this one formula covers both -- falling linearly to
    // 0.0 at distance >= splashRadius. splashRadius must be > 0.
    static float splashDamage(float distance, float splashRadius) {
        return std::clamp(1.0f - distance / splashRadius, 0.0f, 1.0f);
    }

    // Shot dispersion in degrees at a given permanent accuracyBonus (see
    // CombatantState::accuracyBonus, raised by collecting TighterAccuracy).
    // "Introduced as a small default" (PLAN.md): this baseline inaccuracy is
    // new with power-up crates, not present before this item.
    static constexpr float kBaseDispersionDegrees = 1.5f;
    static float dispersionDegrees(float accuracyBonus) {
        return std::max(0.0f, kBaseDispersionDegrees - accuracyBonus);
    }

    // A read-only snapshot of one combatant, returned by combatant().
    struct CombatantState {
        bool alive = true;
        float health = 3.0f; // "full hits" units; destroyed at <= 0. See kMaxHealth.
        float fuelRemaining = kDefaultFuelCapacity;
        float fuelCapacity = kDefaultFuelCapacity;
        int shellsRemaining = 1;
        int shellsPerTurn = 1; // fixed at 1; ExtraShell instead tops up shellsRemaining for one turn
        // Power-up state -- see PowerUpType and MatchState::collectPowerUp/
        // armPowerUp/consumeArmedPowerUp.
        std::array<int, kPowerUpTypeCount> powerUps{};
        std::optional<PowerUpType> armedPowerUp;
        float damageMultiplier = 1.0f;        // this shot only; reset every consumeArmedPowerUp call
        float splashRadiusMultiplier = 1.0f;  // this shot only
        float accuracyBonus = 0.0f;           // permanent; see dispersionDegrees
    };

    explicit MatchState(float playerFuelCapacity = kDefaultFuelCapacity,
                        float opponentFuelCapacity = kDefaultFuelCapacity);

    CombatantId activeCombatant() const { return active_; }
    Phase phase() const { return phase_; }
    const CombatantState& combatant(CombatantId id) const { return combatants_[static_cast<size_t>(id)]; }
    bool isGameOver() const { return phase_ == Phase::GameOver; }
    // Absent while in progress; the surviving combatant once the match ends.
    std::optional<CombatantId> winner() const;

    // Valid only in Phase::Turn, on the active combatant. Negative amounts
    // are treated as zero, and fuel clamps at zero rather than going
    // negative -- reaching zero no longer changes phase (see
    // Application's own fuel-remaining check before it allows driving);
    // firing remains available regardless of remaining fuel.
    void spendFuel(float amount);

    // Valid only in Phase::Turn with a shell remaining: fires it, at any
    // point during the turn regardless of remaining fuel or whether
    // driving has happened yet. Decrements shellsRemaining. Turn ->
    // Resolving.
    void recordShotFired();

    // Valid only in Phase::Resolving, against a still-alive target: applies
    // damageInFullHits (direct hit = 1.0; splash falls off toward 0).
    // Health cannot go below zero; reaching zero destroys the target and
    // ends the match immediately (Resolving -> GameOver).
    void applyDamage(CombatantId target, float damageInFullHits);

    // Valid only in Phase::Resolving: every shell fired this turn has now
    // landed and settled. Returns to Turn if shells remain (e.g.
    // ExtraShell), otherwise ends the turn.
    void notifyProjectilesSettled();

    // Adds one `type` to the active combatant's holdings. MoreFuel/
    // TighterAccuracy apply immediately and permanently instead (see
    // PowerUpType) -- MoreFuel to both fuelCapacity and the current
    // fuelRemaining, so the crate is usable in the turn it's picked up;
    // ExtraShell applies immediately too (+1 shellsRemaining, lapsing at
    // the turn boundary). Only IncreasedDamage/LargerSplash/AimAssist
    // accumulate in powerUps[] until armed.
    void collectPowerUp(CombatantId id, PowerUpType type);

    // Arms `type` for the active combatant's next shot; no-op if none held.
    // Only meaningful for the three inventory-based types.
    void armPowerUp(PowerUpType type);

    // Not phase-gated (always called immediately before recordShotFired,
    // which is itself gated). Resets the active combatant's this-shot
    // modifiers (damageMultiplier/splashRadiusMultiplier) to their
    // defaults, then, if something is armed and still held, consumes one
    // and applies it: IncreasedDamage/LargerSplash set the multipliers
    // above; AimAssist has no further
    // effect here -- the caller reads armedPowerUp directly to decide
    // whether to show a trajectory preview, and this clearing it back to
    // nullopt is what makes that preview disappear the instant a shot fires.
    void consumeArmedPowerUp();

private:
    // Hands control to the other combatant: refills the new active
    // combatant's fuel/shells and returns to Phase::Turn. The combatant who
    // just finished keeps whatever they were left with until their own next
    // turn. Alive/health persist untouched.
    void endTurn();

    std::array<CombatantState, 2> combatants_{};
    CombatantId active_ = CombatantId::Player;
    Phase phase_ = Phase::Turn;
};
