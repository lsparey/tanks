#pragma once

#include <cstdint>
#include <vector>

// Produces a flat, row-major grid of height values: gentle rolling hills
// (a hand-rolled sum of sines) as a base layer, plus a raised flat-topped
// plateau and a carved winding river/valley channel (both hand-rolled value
// noise, same style as the texture generators) layered on top. Both
// features fade out near the world origin so the tank's spawn point always
// lands on the plain rolling-hills base -- see generateHills' own comments.
// `seed` randomizes frequencies/phases/offsets throughout so different
// seeds give visibly different layouts.
class HeightmapGenerator {
public:
    struct Heightmap {
        int resolution = 0;   // samples per side; grid is resolution x resolution
        float worldSize = 0;  // total width/depth in world units, centered at origin
        std::vector<float> heights;  // size resolution*resolution

        float at(int i, int j) const { return heights[j * resolution + i]; }
    };

    static Heightmap generateHills(int resolution, float worldSize, float amplitude,
                                    uint32_t seed);
};
