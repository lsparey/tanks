#include "HeightmapGenerator.h"

#include <cmath>
#include <random>

HeightmapGenerator::Heightmap HeightmapGenerator::generateHills(int resolution, float worldSize,
                                                                 float amplitude, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> freqDist(0.04f, 0.18f);
    std::uniform_real_distribution<float> phaseDist(0.0f, 6.2831853f);

    float freqX1 = freqDist(rng), freqZ1 = freqDist(rng);
    float phaseX1 = phaseDist(rng), phaseZ1 = phaseDist(rng);
    float freqX2 = freqDist(rng), freqZ2 = freqDist(rng);
    float phaseX2 = phaseDist(rng), phaseZ2 = phaseDist(rng);

    Heightmap map;
    map.resolution = resolution;
    map.worldSize = worldSize;
    map.heights.resize(static_cast<size_t>(resolution) * resolution);

    for (int j = 0; j < resolution; ++j) {
        for (int i = 0; i < resolution; ++i) {
            float x = (static_cast<float>(i) / (resolution - 1) - 0.5f) * worldSize;
            float z = (static_cast<float>(j) / (resolution - 1) - 0.5f) * worldSize;

            float h = amplitude * 0.6f * std::sin(x * freqX1 + phaseX1) *
                          std::cos(z * freqZ1 + phaseZ1) +
                      amplitude * 0.4f * std::sin(x * freqX2 + phaseX2) *
                          std::sin(z * freqZ2 + phaseZ2);

            map.heights[j * resolution + i] = h;
        }
    }
    return map;
}
