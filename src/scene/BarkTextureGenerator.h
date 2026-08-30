#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates a tree-bark RGBA8 texture -- vertically-stretched
// fbm noise (bark fissures run mostly along the trunk/branch, not across
// it) over a brown palette, plus a fine high-frequency layer for rough
// surface grain. Same hand-rolled value-noise approach as the other
// texture generators in this project.
class BarkTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size);
};
