#include "HeightmapGenerator.h"

#include <cmath>

HeightmapGenerator::Heightmap HeightmapGenerator::generateHills(int resolution, float worldSize,
                                                                 float amplitude) {
    Heightmap map;
    map.resolution = resolution;
    map.worldSize = worldSize;
    map.heights.resize(static_cast<size_t>(resolution) * resolution);

    for (int j = 0; j < resolution; ++j) {
        for (int i = 0; i < resolution; ++i) {
            float x = (static_cast<float>(i) / (resolution - 1) - 0.5f) * worldSize;
            float z = (static_cast<float>(j) / (resolution - 1) - 0.5f) * worldSize;

            float h = amplitude * 0.6f * std::sin(x * 0.15f) * std::cos(z * 0.15f) +
                      amplitude * 0.4f * std::sin(x * 0.05f + 1.3f) * std::sin(z * 0.08f + 0.7f);

            map.heights[j * resolution + i] = h;
        }
    }
    return map;
}
