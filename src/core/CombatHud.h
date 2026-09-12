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
    float boundaryHalfExtent = 1, fps = 0, gpuMs = 0;
    std::span<const Box> targets;
    std::string_view camera = "HULL FOLLOW";
    bool help = false, diagnostics = false, treeLod = true, reflectionRays = true;
};

// Full-area map: +Z is north, +X is east. Coordinates outside the boundary
// clamp to the border, so every marker stays inside the panel.
glm::vec2 mapPosition(glm::vec3 position, float boundaryHalfExtent);
float headingDegrees(glm::vec3 direction);
void draw(HudGeometry& hud, glm::vec2 viewportPixels, const State& state);
}
