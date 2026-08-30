#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates a mottled grass-like RGBA8 texture using hand-rolled
// value noise (no external noise library, same spirit as HeightmapGenerator)
// -- large-scale patches blended between a few green shades, plus a finer
// high-frequency layer for blade-like speckle detail.
//
// `variant` selects both a different color palette and a shifted noise
// seed, so a handful of variants read as genuinely different grass (lush
// vs. dry/sun-bleached) rather than the same texture recolored -- see
// Terrain's patch-blended painting in basic.frag, which mixes between two
// variants using a large-scale noise mask.
class GrassTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size, uint32_t variant = 0);
};
