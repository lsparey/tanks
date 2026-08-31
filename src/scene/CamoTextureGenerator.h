#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates a 4-tone military disruptive-camouflage RGBA8
// texture -- irregular, hard-edged (not smoothly gradiented) blotches of
// dark green, brown, tan, and near-black, the way a real painted camo
// scheme reads up close, rather than the soft continuous color blend the
// other organic-material generators in this project use (grass, gravel,
// bark). Tileable under GL_REPEAT, since the tank model's own UV layout
// (baked in MilkShape3D) isn't a single clean 0..1 island per part -- see
// Application's camoMaterialSet_ and Tank::load's vertex color.
class CamoTextureGenerator {
public:
    static std::vector<uint8_t> generate(uint32_t size);
};
