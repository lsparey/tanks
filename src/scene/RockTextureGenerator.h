#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates a mottled grey gravel/rock RGBA8 texture -- same
// value-noise approach as GrassTextureGenerator (large-scale patches plus a
// finer speckle layer), just with a desaturated grey palette and a sharper,
// higher-contrast speckle to read as loose gravel rather than smooth stone.
class RockTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size);
};
