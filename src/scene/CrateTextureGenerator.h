#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "PowerUp.h"

// Procedurally generates a wooden-crate RGBA8 texture -- wood grain plus a
// few dark horizontal plank seams and a darkened border, so a textured cube
// reads as an actual crate rather than a flat-colored box. Unlike the other
// generators, this one is mapped exactly once per cube face (see
// Mesh::cube's UV) rather than tiled across a repeating surface, so it
// doesn't need seamlessly-tileable noise -- CLAMP addressing is used
// instead of REPEAT.
class CrateTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size);
    // Copies `base` (a generate() result of the same `size`) and spray-
    // stencils the power-up's icon over the middle of it, in that power-up's
    // own paint colour, so a crate shows what it holds before it's collected.
    // Since Mesh::cube maps the texture once per face, the icon lands on
    // every side. Icons are drawn as analytic signed-distance shapes at the
    // texture's own resolution -- no bitmap assets involved.
    static std::vector<uint8_t> stampIcon(const std::vector<uint8_t>& base, uint32_t size, PowerUpType type);

    // The icon glyphs themselves, exposed so the pause menu's help page can
    // draw the very same shapes (rasterised into HUD quads) next to their
    // descriptions. `p` is icon space: the glyph's centre is the origin and
    // it fits roughly within the unit disc; returns a signed distance
    // (negative inside).
    static float iconDistance(PowerUpType type, glm::vec2 p);
    static glm::vec3 iconPaint(PowerUpType type);
};
