#include "scene/MatchState.h"

#include <stdexcept>
#include <tuple>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Every field that could possibly change under a wrongly-timed mutator
// call, so a "no-op" assertion can check the whole match at once rather
// than one field at a time.
auto snapshot(const MatchState& m) {
    return std::tuple{
        m.phase(), m.activeCombatant(),
        m.combatant(CombatantId::Player).alive, m.combatant(CombatantId::Player).health,
        m.combatant(CombatantId::Player).fuelRemaining, m.combatant(CombatantId::Player).shellsRemaining,
        m.combatant(CombatantId::Opponent).alive, m.combatant(CombatantId::Opponent).health,
        m.combatant(CombatantId::Opponent).fuelRemaining, m.combatant(CombatantId::Opponent).shellsRemaining,
    };
}

// Scripted helper that fires the active combatant's shot, entering
// Resolving -- firing needs no prior transition (see PLAN.md's "fire at
// any time" design change), so this is now just a readability wrapper.
void toResolving(MatchState& m) { m.recordShotFired(); }

int main() {
    // 1. Constructor defaults.
    {
        MatchState m(20.0f, 15.0f);
        require(m.phase() == Phase::Turn, "Match starts in Turn phase");
        require(m.activeCombatant() == CombatantId::Player, "Player moves first");
        require(!m.isGameOver(), "Fresh match is not over");
        require(!m.winner().has_value(), "Fresh match has no winner");
        auto& player = m.combatant(CombatantId::Player);
        require(player.alive && player.health == MatchState::kMaxHealth, "Player starts full health");
        require(player.fuelRemaining == 20.0f && player.fuelCapacity == 20.0f, "Player fuel capacity applied");
        require(player.shellsRemaining == 1 && player.shellsPerTurn == 1, "Player starts with one shell");
        auto& opponent = m.combatant(CombatantId::Opponent);
        require(opponent.fuelRemaining == 15.0f && opponent.fuelCapacity == 15.0f, "Opponent fuel capacity applied");
        require(opponent.alive && opponent.health == MatchState::kMaxHealth, "Opponent starts full health");
    }

    // 2. Fuel clamps at exactly zero, not before, never negative, and no
    // longer changes phase on its own (firing stays available regardless
    // of remaining fuel -- see PLAN.md's "fire at any time" design change).
    {
        MatchState m;
        m.spendFuel(MatchState::kDefaultFuelCapacity - 0.5f);
        require(m.phase() == Phase::Turn, "Turn phase persists while fuel remains");
        require(m.combatant(CombatantId::Player).fuelRemaining == 0.5f, "Partial fuel spend recorded");
        m.spendFuel(0.5f);
        require(m.phase() == Phase::Turn, "Fuel reaching exactly zero does not change phase");
        require(m.combatant(CombatantId::Player).fuelRemaining == 0.0f, "Fuel settles at exactly zero");
    }
    {
        MatchState m(20.0f, 20.0f);
        m.spendFuel(50.0f); // overshoot in one call
        require(m.phase() == Phase::Turn, "Overshooting fuel still leaves phase unchanged");
        require(m.combatant(CombatantId::Player).fuelRemaining == 0.0f, "Fuel clamps at zero, never negative");
    }
    {
        // Negative amounts must not refund fuel.
        MatchState m;
        m.spendFuel(5.0f);
        auto before = snapshot(m);
        m.spendFuel(-100.0f);
        require(snapshot(m) == before, "Spending a negative amount has no effect");
    }

    // 3. Every mutator is a no-op outside its valid phase.
    {
        // spendFuel: valid only in Turn.
        MatchState resolving;
        toResolving(resolving);
        auto atResolving = snapshot(resolving);
        resolving.spendFuel(5.0f);
        require(snapshot(resolving) == atResolving, "spendFuel is a no-op in Resolving");
    }
    {
        // recordShotFired: valid only in Turn (already exercised landing in
        // Resolving above; firing again while still Resolving must no-op).
        MatchState resolvingState;
        toResolving(resolvingState);
        auto beforeResolving = snapshot(resolvingState);
        resolvingState.recordShotFired();
        require(snapshot(resolvingState) == beforeResolving, "recordShotFired is a no-op in Resolving");
    }
    {
        // notifyProjectilesSettled: valid only in Resolving.
        MatchState turnState;
        auto beforeTurn = snapshot(turnState);
        turnState.notifyProjectilesSettled();
        require(snapshot(turnState) == beforeTurn, "notifyProjectilesSettled is a no-op in Turn");
    }
    {
        // applyDamage: valid only in Resolving.
        MatchState turnState;
        auto beforeTurn = snapshot(turnState);
        turnState.applyDamage(CombatantId::Opponent, 1.0f);
        require(snapshot(turnState) == beforeTurn, "applyDamage is a no-op in Turn");
    }

    // 4. Shell-remaining / turn-refill correctness across a turn boundary.
    {
        MatchState m(20.0f, 30.0f);
        m.spendFuel(4.0f); // Player leaves 16 fuel unused this turn
        m.recordShotFired();
        require(m.combatant(CombatantId::Player).shellsRemaining == 0, "Firing consumes the turn's shell");
        require(m.phase() == Phase::Resolving, "Firing enters Resolving");
        m.notifyProjectilesSettled();
        require(m.activeCombatant() == CombatantId::Opponent, "Turn passes to the opponent");
        require(m.phase() == Phase::Turn, "New turn starts in Turn");
        auto& newActive = m.combatant(CombatantId::Opponent);
        require(newActive.shellsRemaining == newActive.shellsPerTurn, "Incoming combatant's shells refill");
        require(newActive.fuelRemaining == newActive.fuelCapacity, "Incoming combatant's fuel refills");
        auto& justFinished = m.combatant(CombatantId::Player);
        require(justFinished.shellsRemaining == 0, "Combatant who just fired keeps zero shells until their own next turn");
        require(justFinished.fuelRemaining == 16.0f, "Unused fuel is not refilled until the combatant's own next turn");
    }

    // 5. Fractional splash damage accumulating to a kill, across turns.
    {
        MatchState m;
        auto shootAndPass = [](MatchState& s, CombatantId target, float damage) {
            s.recordShotFired();
            s.applyDamage(target, damage);
            s.notifyProjectilesSettled();
        };
        shootAndPass(m, CombatantId::Opponent, 1.0f); // Player's turn 1: direct hit; 3.0 -> 2.0
        require(m.combatant(CombatantId::Opponent).health == 2.0f, "First direct hit lands");
        require(m.combatant(CombatantId::Opponent).alive, "2.0 remaining health is still alive");
        shootAndPass(m, CombatantId::Player, 0.4f); // Opponent's turn: irrelevant splash on Player
        shootAndPass(m, CombatantId::Opponent, 1.5f); // Player's turn 2: splash; 2.0 -> 0.5
        require(m.combatant(CombatantId::Opponent).health == 0.5f, "Splash hit accumulates fractionally");
        require(m.combatant(CombatantId::Opponent).alive && !m.isGameOver(), "0.5 remaining health is still alive");
        shootAndPass(m, CombatantId::Player, 0.1f); // Opponent's turn: irrelevant
        shootAndPass(m, CombatantId::Opponent, 0.5f); // Player's turn 3: exact final splash; 0.5 -> 0.0
        require(m.combatant(CombatantId::Opponent).health == 0.0f, "Cumulative splash reaches exactly zero");
        require(!m.combatant(CombatantId::Opponent).alive, "Cumulative splash across three turns destroys the target");
        require(m.isGameOver() && m.winner() == CombatantId::Player, "Match ends with the correct winner");
    }

    // 6. Multiple applyDamage calls within a single Resolving phase (a
    // future multi-shell turn, or simply defensive robustness) still clamp
    // and kill correctly, and further damage after death is a no-op.
    {
        MatchState m;
        toResolving(m);
        m.applyDamage(CombatantId::Opponent, 2.0f);
        require(m.combatant(CombatantId::Opponent).health == 1.0f, "First same-phase hit lands");
        require(!m.isGameOver(), "Not dead yet after the first hit");
        m.applyDamage(CombatantId::Opponent, 1.5f); // overshoots by 0.5
        require(m.combatant(CombatantId::Opponent).health == 0.0f, "Overshooting damage clamps at exactly zero");
        require(m.isGameOver() && m.winner() == CombatantId::Player, "Second same-phase hit finishes the kill");
        auto before = snapshot(m);
        m.applyDamage(CombatantId::Opponent, 1.0f);
        require(snapshot(m) == before, "Damage after death is a no-op");
    }

    // 7. Winner determination is not order-dependent: the opponent can win too.
    {
        MatchState m;
        toResolving(m);
        m.applyDamage(CombatantId::Player, MatchState::kMaxHealth);
        require(m.isGameOver() && m.winner() == CombatantId::Opponent, "Opponent can win when the player is destroyed");
        require(!m.combatant(CombatantId::Player).alive, "Player is marked dead");
    }

    // 8. Full multi-turn match driven by scripted inputs to a win (the
    // acceptance criterion, exercised end-to-end).
    {
        MatchState m(20.0f, 20.0f);

        // Turn 1 -- Player: partial move, fire immediately (no explicit
        // phase transition needed -- see PLAN.md's "fire at any time"
        // design change), direct hit, resolve.
        require(m.phase() == Phase::Turn && m.activeCombatant() == CombatantId::Player, "Turn 1 is Player's turn");
        m.spendFuel(5.0f);
        m.recordShotFired();
        require(m.phase() == Phase::Resolving, "Firing enters Resolving");
        m.applyDamage(CombatantId::Opponent, 1.0f);
        require(!m.isGameOver(), "One direct hit does not end the match");
        m.notifyProjectilesSettled();
        require(m.activeCombatant() == CombatantId::Opponent && m.phase() == Phase::Turn, "Turn passes to Opponent");

        // Turn 2 -- Opponent: full-fuel move, fire, splash hit. Exhausting
        // fuel no longer changes phase -- firing is always available in Turn.
        m.spendFuel(20.0f);
        require(m.phase() == Phase::Turn, "Exhausting fuel does not change phase");
        m.recordShotFired();
        m.applyDamage(CombatantId::Player, 0.5f);
        m.notifyProjectilesSettled();
        require(m.activeCombatant() == CombatantId::Player, "Turn passes back to Player");

        // Turn 3 -- Player: fire immediately, no movement at all, splash hit.
        m.recordShotFired();
        m.applyDamage(CombatantId::Opponent, 1.5f); // Opponent: 2.0 -> 0.5
        m.notifyProjectilesSettled();

        // Turn 4 -- Opponent: fire immediately, no movement at all, splash hit.
        m.recordShotFired();
        m.applyDamage(CombatantId::Player, 0.5f); // Player: 2.5 -> 2.0
        m.notifyProjectilesSettled();

        // Turn 5 -- Player: fire immediately, finishing hit.
        m.recordShotFired();
        m.applyDamage(CombatantId::Opponent, 0.5f); // Opponent: 0.5 -> 0.0, destroyed

        require(m.isGameOver(), "Match ends the instant the finishing hit lands");
        require(m.phase() == Phase::GameOver, "Phase reports GameOver");
        require(m.winner() == CombatantId::Player, "Player is the correct winner");
        require(!m.combatant(CombatantId::Opponent).alive, "Opponent is destroyed");
        require(m.combatant(CombatantId::Player).alive, "Player survives");
        require(m.combatant(CombatantId::Player).health == 2.0f, "Player's accumulated damage matches the script");

        // Phases transition only on defined events: the match staying over
        // is itself such a guarantee, so nothing should un-finish it.
        auto finished = snapshot(m);
        m.notifyProjectilesSettled();
        m.recordShotFired();
        m.spendFuel(100.0f);
        m.applyDamage(CombatantId::Player, 1.0f);
        require(snapshot(m) == finished, "A finished match cannot be reopened by any mutator");
    }

    // 9. Fuel-cost formula: pure, deterministic, scales with distance and pivot.
    {
        require(MatchState::movementFuelCost(0.0f, 0.0f, 1.0f) == 0.0f, "No motion costs no fuel");
        require(MatchState::movementFuelCost(5.0f, 0.0f, 2.0f) == 10.0f, "Distance cost is speed * time");
        require(MatchState::movementFuelCost(-5.0f, 0.0f, 2.0f) == 10.0f, "Distance cost uses speed magnitude");
        require(MatchState::movementFuelCost(0.0f, 3.0f, 2.0f) == 6.0f, "Pivot cost is angular speed * time");
        require(MatchState::movementFuelCost(0.0f, -3.0f, 2.0f) == 6.0f, "Pivot cost uses angular speed magnitude");
        require(MatchState::movementFuelCost(5.0f, 3.0f, 2.0f) == 16.0f, "Distance and pivot costs add");
    }

    // 10. Scripted drive to empty: a constant-speed drive (as a real
    // Tank::update loop would produce, one movementFuelCost/spendFuel call
    // per simulated frame) drains fuel deterministically to exactly zero on
    // the expected frame -- not one frame earlier or later -- and clamps
    // there without changing phase (see PLAN.md's "fire at any time"
    // design change: firing stays available regardless of fuel).
    {
        MatchState m(20.0f, 20.0f);
        constexpr float kSpeed = 6.0f, kDeltaTime = 1.0f / 60.0f;  // 0.1 fuel/frame
        int frame = 0;
        while (m.combatant(CombatantId::Player).fuelRemaining > 0.0f) {
            require(frame < 250, "scripted drive failed to exhaust fuel in a reasonable number of frames");
            m.spendFuel(MatchState::movementFuelCost(kSpeed, 0.0f, kDeltaTime));
            ++frame;
        }
        require(frame == 200, "scripted drive did not exhaust fuel on the expected frame");
        require(m.phase() == Phase::Turn, "phase remains Turn regardless of fuel");
        require(m.combatant(CombatantId::Player).fuelRemaining == 0.0f, "fuel settles at exactly zero");
        // "The tank cannot move past zero": further spend calls are no-ops
        // (already proven generically in section 3; reasserted here in the
        // scripted-drive's own terms).
        auto before = snapshot(m);
        m.spendFuel(MatchState::movementFuelCost(kSpeed, 0.0f, kDeltaTime));
        require(snapshot(m) == before, "fuel cannot be spent past zero");
    }

    // 11. A pivot-only scripted drive drains fuel the same deterministic way.
    {
        MatchState m(20.0f, 20.0f);
        constexpr float kAngularSpeed = 6.0f, kDeltaTime = 1.0f / 60.0f;
        int frame = 0;
        while (m.combatant(CombatantId::Player).fuelRemaining > 0.0f) {
            require(frame < 250, "scripted pivot failed to exhaust fuel in a reasonable number of frames");
            m.spendFuel(MatchState::movementFuelCost(0.0f, kAngularSpeed, kDeltaTime));
            ++frame;
        }
        require(frame == 200, "scripted pivot did not exhaust fuel on the expected frame");
    }

    // 12. splashDamage's curve, pinned at the hull, half radius and the
    // edge -- the literal acceptance wording -- plus beyond the edge.
    {
        constexpr float kRadius = 4.5f;
        require(MatchState::splashDamage(0.0f, kRadius) == 1.0f, "Hull distance is a full hit");
        require(MatchState::splashDamage(kRadius * 0.5f, kRadius) == 0.5f, "Half radius is half damage");
        require(MatchState::splashDamage(kRadius, kRadius) == 0.0f, "Edge distance does no damage");
        require(MatchState::splashDamage(kRadius * 1.5f, kRadius) == 0.0f, "Beyond the edge stays clamped at zero");
    }

    // 13. A scripted match where every hit is a direct hit (splashDamage at
    // distance 0) ends in destruction after exactly three -- the literal
    // acceptance wording ("a scripted three-direct-hit match ends in
    // destruction").
    {
        MatchState m;
        constexpr float kRadius = 4.5f;
        auto directHit = [&](CombatantId target) {
            toResolving(m);
            m.applyDamage(target, MatchState::splashDamage(0.0f, kRadius));
            m.notifyProjectilesSettled();
        };
        directHit(CombatantId::Opponent);  // Player's turn 1: 3.0 -> 2.0
        require(!m.isGameOver(), "One direct hit does not yet destroy");
        directHit(CombatantId::Player);    // Opponent's turn: irrelevant
        directHit(CombatantId::Opponent);  // Player's turn 2: 2.0 -> 1.0
        require(!m.isGameOver(), "Two direct hits do not yet destroy");
        directHit(CombatantId::Player);    // Opponent's turn: irrelevant
        directHit(CombatantId::Opponent);  // Player's turn 3: 1.0 -> 0.0
        require(m.isGameOver() && m.winner() == CombatantId::Player,
                "Three direct hits destroy the target and end the match");
    }

    // 14. Three edge-distance splash hits (splashDamage at distance ==
    // radius, i.e. zero damage) leave the target undamaged and the match in
    // progress -- the acceptance wording's negative case.
    {
        MatchState m;
        constexpr float kRadius = 4.5f;
        auto edgeHit = [&](CombatantId target) {
            toResolving(m);
            m.applyDamage(target, MatchState::splashDamage(kRadius, kRadius));
            m.notifyProjectilesSettled();
        };
        edgeHit(CombatantId::Opponent);
        edgeHit(CombatantId::Player);
        edgeHit(CombatantId::Opponent);
        edgeHit(CombatantId::Player);
        edgeHit(CombatantId::Opponent);
        require(!m.isGameOver(), "Three edge-distance splash hits do not destroy the target");
        require(m.combatant(CombatantId::Opponent).health == MatchState::kMaxHealth,
                "Edge-distance splash hits do zero damage");
    }

    // 15. collectPowerUp: the three inventory types accumulate in powerUps[];
    // ExtraShell/MoreFuel/TighterAccuracy apply immediately instead, never
    // touching powerUps[].
    {
        MatchState m;
        for (PowerUpType type : {PowerUpType::IncreasedDamage, PowerUpType::LargerSplash, PowerUpType::AimAssist}) {
            m.collectPowerUp(CombatantId::Player, type);
            require(m.combatant(CombatantId::Player).powerUps[static_cast<size_t>(type)] == 1,
                    "Inventory power-up is held after collection");
        }
        m.spendFuel(7.0f);  // part-way through a drive, so "remaining" != "capacity"
        float capacityBefore = m.combatant(CombatantId::Player).fuelCapacity;
        float remainingBefore = m.combatant(CombatantId::Player).fuelRemaining;
        m.collectPowerUp(CombatantId::Player, PowerUpType::MoreFuel);
        require(m.combatant(CombatantId::Player).fuelCapacity == capacityBefore + 10.0f,
                "MoreFuel applies immediately to fuelCapacity");
        require(m.combatant(CombatantId::Player).fuelRemaining == remainingBefore + 10.0f,
                "MoreFuel also tops up the current tank, usable this turn");
        require(m.combatant(CombatantId::Player).powerUps[static_cast<size_t>(PowerUpType::MoreFuel)] == 0,
                "MoreFuel is never held in inventory");
        require(m.combatant(CombatantId::Player).accuracyBonus == 0.0f, "No accuracy bonus yet");
        m.collectPowerUp(CombatantId::Player, PowerUpType::TighterAccuracy);
        require(m.combatant(CombatantId::Player).accuracyBonus == 0.5f,
                "TighterAccuracy applies immediately to accuracyBonus");
        require(m.combatant(CombatantId::Player).powerUps[static_cast<size_t>(PowerUpType::TighterAccuracy)] == 0,
                "TighterAccuracy is never held in inventory");
    }

    // 16. armPowerUp is a no-op at zero held, arms at nonzero, and cannot
    // re-arm once a type's count reaches zero.
    {
        MatchState m;
        m.armPowerUp(PowerUpType::IncreasedDamage);
        require(!m.combatant(CombatantId::Player).armedPowerUp.has_value(),
                "Arming with zero held is a no-op");
        m.collectPowerUp(CombatantId::Player, PowerUpType::IncreasedDamage);
        m.armPowerUp(PowerUpType::IncreasedDamage);
        require(m.combatant(CombatantId::Player).armedPowerUp == PowerUpType::IncreasedDamage,
                "Arming with one held arms it");
        m.consumeArmedPowerUp();
        m.armPowerUp(PowerUpType::IncreasedDamage);
        require(!m.combatant(CombatantId::Player).armedPowerUp.has_value(),
                "Cannot re-arm a power-up type once its count reaches zero");
    }

    // 17. consumeArmedPowerUp: each of the four armable types' effect, and
    // the "this shot only" ones' expiry -- the literal acceptance wording
    // ("each power-up has a test proving its effect and its expiry").
    {
        MatchState m;  // nothing armed, nothing held: a safe no-op
        m.consumeArmedPowerUp();
        require(m.combatant(CombatantId::Player).damageMultiplier == 1.0f &&
                m.combatant(CombatantId::Player).splashRadiusMultiplier == 1.0f,
                "Consuming with nothing armed changes nothing");
    }
    {
        // ExtraShell: applied on pickup, no arming step -- an actual second
        // shot this turn, and it lapses (not banked) at the turn boundary.
        MatchState m;
        int shellsBefore = m.combatant(CombatantId::Player).shellsRemaining;
        m.collectPowerUp(CombatantId::Player, PowerUpType::ExtraShell);
        require(m.combatant(CombatantId::Player).shellsRemaining == shellsBefore + 1,
                "ExtraShell grants one more shell immediately on collection");
        require(m.combatant(CombatantId::Player).powerUps[static_cast<size_t>(PowerUpType::ExtraShell)] == 0,
                "ExtraShell is never held in inventory");
        m.armPowerUp(PowerUpType::ExtraShell);
        require(!m.combatant(CombatantId::Player).armedPowerUp.has_value(), "ExtraShell cannot be armed");
        m.recordShotFired();
        m.notifyProjectilesSettled();
        require(m.activeCombatant() == CombatantId::Player && m.phase() == Phase::Turn,
                "Player keeps the turn for the extra shot");
        m.recordShotFired();
        m.notifyProjectilesSettled();
        require(m.activeCombatant() == CombatantId::Opponent, "Turn passes once the extra shot is spent");
        m.recordShotFired();
        m.notifyProjectilesSettled();
        require(m.combatant(CombatantId::Player).shellsRemaining == m.combatant(CombatantId::Player).shellsPerTurn,
                "An unused extra shell would not carry over: shells reset to shellsPerTurn each turn");
    }
    {
        MatchState m;
        m.collectPowerUp(CombatantId::Player, PowerUpType::IncreasedDamage);
        m.armPowerUp(PowerUpType::IncreasedDamage);
        m.consumeArmedPowerUp();
        require(m.combatant(CombatantId::Player).damageMultiplier == 1.5f,
                "IncreasedDamage scales this shot's damage");
        m.consumeArmedPowerUp();  // the next shot, with nothing newly armed
        require(m.combatant(CombatantId::Player).damageMultiplier == 1.0f,
                "IncreasedDamage's bonus expires after one shot");
    }
    {
        MatchState m;
        m.collectPowerUp(CombatantId::Player, PowerUpType::LargerSplash);
        m.armPowerUp(PowerUpType::LargerSplash);
        m.consumeArmedPowerUp();
        require(m.combatant(CombatantId::Player).splashRadiusMultiplier == 1.5f,
                "LargerSplash scales this shot's splash radius");
        m.consumeArmedPowerUp();
        require(m.combatant(CombatantId::Player).splashRadiusMultiplier == 1.0f,
                "LargerSplash's bonus expires after one shot");
    }
    {
        MatchState m;
        m.collectPowerUp(CombatantId::Player, PowerUpType::AimAssist);
        m.armPowerUp(PowerUpType::AimAssist);
        require(m.combatant(CombatantId::Player).armedPowerUp == PowerUpType::AimAssist, "AimAssist can be armed");
        m.consumeArmedPowerUp();
        require(!m.combatant(CombatantId::Player).armedPowerUp.has_value(),
                "AimAssist's preview is cleared the instant the shot fires");
        require(m.combatant(CombatantId::Player).powerUps[static_cast<size_t>(PowerUpType::AimAssist)] == 0,
                "AimAssist is consumed from inventory");
    }

    // 18. dispersionDegrees, pinned.
    {
        require(MatchState::dispersionDegrees(0.0f) == 1.5f, "Baseline dispersion with no accuracy bonus");
        require(MatchState::dispersionDegrees(0.5f) == 1.0f, "Partial accuracy bonus reduces dispersion");
        require(MatchState::dispersionDegrees(1.5f) == 0.0f, "Full accuracy bonus removes dispersion");
        require(MatchState::dispersionDegrees(2.0f) == 0.0f, "Dispersion cannot go negative");
    }
}
