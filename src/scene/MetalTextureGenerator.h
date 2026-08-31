#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates a plain brushed-gunmetal RGBA8 texture -- a dark,
// fairly desaturated grey with fine directional streak noise (brushing
// marks) plus a subtler speckle layer, for the tank's bare-metal parts
// (tracks, barrel) as opposed to its painted camo hull/turret -- see
// CamoTextureGenerator and Application's metalMaterialSet_. Tileable under
// GL_REPEAT, same reasoning as CamoTextureGenerator.
class MetalTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size);
};
