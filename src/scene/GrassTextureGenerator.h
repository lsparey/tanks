#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates a mottled grass-like RGBA8 texture using hand-rolled
// value noise (no external noise library, same spirit as HeightmapGenerator)
// -- large-scale patches blended between a few green shades, plus a finer
// high-frequency layer for blade-like speckle detail.
class GrassTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size);
};
