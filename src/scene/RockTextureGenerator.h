#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates a mottled grey gravel/rock RGBA8 texture -- same
// value-noise approach as GrassTextureGenerator (large-scale patches plus a
// finer speckle layer), just with a desaturated grey palette and a sharper,
// higher-contrast speckle to read as loose gravel rather than smooth stone.
//
// `variant` selects both a different color palette and a shifted noise
// seed, so a handful of variants read as genuinely different gravel
// (darker/wetter-looking vs. lighter/dustier) -- see Terrain's
// patch-blended painting in basic.frag.
class RockTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size, uint32_t variant = 0);
};
