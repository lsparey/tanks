#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates one dark-brown, ridge-segmented tread imprint.
// Application stamps this texture independently beneath the left and right
// tracks, allowing their trails to curve in opposite directions during a
// pivot instead of baking both rails into one rigid full-hull decal.
class TrackTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size);
};
