#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

// Procedurally generates a 4-tone military disruptive-camouflage RGBA8
// texture -- irregular, hard-edged (not smoothly gradiented) blotches of
// four palette colors, the way a real painted camo scheme reads up close,
// rather than the soft continuous color blend the other organic-material
// generators in this project use (grass, gravel, bark). Tileable under
// GL_REPEAT, since the tank model's own UV layout (baked in MilkShape3D)
// isn't a single clean 0..1 island per part -- see Application's
// camoMaterialSet_ and Tank::load's vertex color.
class CamoTextureGenerator {
public:
    // Four blotch tones, darkest-coverage to smallest-coverage (see
    // generate()'s banding thresholds): a broad base tone, a mid tone, a
    // lighter tone, and a near-black accent.
    struct Palette { glm::vec3 darkGreen, brown, tan, black; };

    // Defaults to the original dark-green/brown/tan/near-black scheme, so
    // every call site predating this parameter is unchanged.
    static std::vector<uint8_t> generate(uint32_t size,
        const Palette& palette = Palette{{0.07f, 0.10f, 0.05f}, {0.15f, 0.10f, 0.05f},
                                          {0.30f, 0.25f, 0.15f}, {0.03f, 0.03f, 0.03f}});
};
