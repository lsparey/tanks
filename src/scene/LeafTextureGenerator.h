#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates a foliage RGBA8 texture -- dense, high-frequency
// mottled green speckle (denser than GrassTextureGenerator's blade pattern,
// since this is meant to read as a mass of small leaves/needles up close on
// a tree canopy) using the same hand-rolled tileable value noise as the
// other generators.
class LeafTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size);
};
