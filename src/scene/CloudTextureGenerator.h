#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates a cloud-layer RGBA8 texture -- large, soft-edged
// white/grey fbm blobs on a fully transparent background (alpha 0), meant
// to be projected onto the underside of a sky dome (see Mesh::dome) rather
// than tiled onto a flat surface. Same hand-rolled value-noise approach as
// GrassTextureGenerator/RockTextureGenerator/TrackTextureGenerator.
class CloudTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size);
};
