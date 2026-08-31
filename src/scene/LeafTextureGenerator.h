#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates a foliage RGBA8 texture -- dense, high-frequency
// mottled green speckle (denser than GrassTextureGenerator's blade pattern,
// since this is meant to read as a mass of small leaves/needles up close on
// a tree canopy) using the same hand-rolled tileable value noise as the
// other generators.
//
// `variant` selects both a different foliage color palette and a shifted
// noise seed, so Application's small pool of tree mesh variants (see
// treeLeafMeshes_) can each get genuinely different-looking foliage instead
// of all sharing one texture.
class LeafTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size, uint32_t variant = 0);
};
