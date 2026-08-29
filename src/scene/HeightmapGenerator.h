#pragma once

#include <vector>

// Produces a flat, row-major grid of height values using a hand-rolled sum
// of sines ("gentle hills") -- no noise library needed at this prototype's
// scope.
class HeightmapGenerator {
public:
    struct Heightmap {
        int resolution = 0;   // samples per side; grid is resolution x resolution
        float worldSize = 0;  // total width/depth in world units, centered at origin
        std::vector<float> heights;  // size resolution*resolution

        float at(int i, int j) const { return heights[j * resolution + i]; }
    };

    static Heightmap generateHills(int resolution, float worldSize, float amplitude);
};
