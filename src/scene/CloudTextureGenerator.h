#pragma once

#include <cstdint>
#include <vector>

// Tileable cloud texture: RGB contains optional cloud shading and alpha
// stores linear density. basic.frag samples that same density by direction
// for the visible sky and reflection misses; coverage and colour live in
// FrameUBO. Alpha avoids sRGB decoding of density data.
class CloudTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size);
};
