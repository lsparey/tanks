#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates a single dark-brown "track mark" decal texture --
// two separate ridge-segmented rails (with a gap of transparent/undisturbed
// ground between them) evoking a tank's twin tracks, rather than one solid
// band across the whole width. Meant to be stamped once per TrackMark quad
// (not tiled), with the quad's own width/length scale doing the elongation
// -- see TrackMark::worldMatrix.
class TrackTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size);
};
