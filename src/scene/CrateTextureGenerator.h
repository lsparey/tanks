#pragma once

#include <cstdint>
#include <vector>

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
};
