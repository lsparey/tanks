#pragma once

#include <span>
#include <string_view>
#include <glm/glm.hpp>
#include "../render/HudGeometry.h"
#include "../scene/Box.h"

namespace CombatHud {
struct State {
    glm::vec3 position{0}, forward{0,0,1}, aimDirection{0,0,1};
    glm::vec4 aimClip{0,0,0,1};
    float speed = 0, turretYaw = 0, gunElevation = 0;
    float shotPower = 0;  // normalized 0..1; see Tank::shotPower/shotSpeed
    float boundaryHalfExtent = 1, fps = 0, gpuMs = 0;
    std::span<const Box> targets;
    // Pre-formatted by the caller (e.g. "SHL:1 SPL:2 [ARMED DMG]") rather
    // than typed on PowerUpType, so CombatHud stays a dumb renderer of a
    // state struct with no MatchState dependency. Empty when there's
    // nothing to show.
    std::string_view inventoryText;
    // Predicted ballistic arc (see MatchState::PowerUpType::AimAssist),
    // already projected to clip space the same way aimClip is -- empty
    // unless aim assist is currently armed. Drawn as connected line
    // segments between consecutive points.
    std::span<const glm::vec4> trajectoryClip;
    std::string_view camera = "HULL FOLLOW";
    bool help = false, diagnostics = false, treeLod = true, reflectionRays = true;

    // Per-combatant match stats -- plain data, not typed on MatchState's own
    // CombatantState, for the same reason inventoryText is pre-formatted:
    // CombatHud stays a dumb renderer with no MatchState dependency.
    struct CombatantHud {
        bool alive = true;
        float health = 3.0f, healthMax = 3.0f;
        float fuelRemaining = 20.0f, fuelCapacity = 20.0f;
        int shellsRemaining = 1, shellsPerTurn = 1;
    };
    bool matchActive = false;      // hides the whole turn/combat panel outside --match
    bool opponentPresent = false;  // guards the opponent panel independent of matchActive
    CombatantHud playerCombat, opponentCombat;
    // Pre-formatted by the caller (e.g. "YOUR TURN", "OPPONENT TURN",
    // "RESOLVING", "YOU WIN"), same reasoning as inventoryText.
    std::string_view turnLabel;

    // Big center-screen banner, distinct from the small persistent
    // turnLabel chip above -- shown briefly at the start of a match/after
    // a restart, or persistently once the match ends. Mutually exclusive
    // in practice (Application never sets both). The captions themselves
    // ("MATCH START", "PRESS N TO RESTART") are hardcoded in CombatHud,
    // same precedent as "GUN OUT OF VIEW"/"H CONTROLS" -- only the winner
    // announcement is caller-supplied text, reusing turnLabel's own value.
    bool showMatchStartBanner = false;
    bool showMatchOverBanner = false;
    std::string_view matchOverText;
};

// Full-area map: +Z is north, +X is east. Coordinates outside the boundary
// clamp to the border, so every marker stays inside the panel.
glm::vec2 mapPosition(glm::vec3 position, float boundaryHalfExtent);
float headingDegrees(glm::vec3 direction);
void draw(HudGeometry& hud, glm::vec2 viewportPixels, const State& state);
}
