#pragma once

#include <cstddef>
#include "HeightmapGenerator.h"

namespace MacroTerrain {

struct Settings {
    // All lengths are world units, independent of sampling resolution.
    float relief = 12.0f;
    float valleyWidth = 32.0f;
    float featureScale = 64.0f;
    float soilDepth = 0.9f;
    float apronWidth = 12.0f; // rounded UP to a whole number of sample intervals
};

enum OpenFace : uint8_t { NegativeX = 1, PositiveX = 2, NegativeZ = 4, PositiveZ = 8 };

// Mutable generation domain, including apron. Keep this whole domain through
// erosion and drainage; crop only the final render/contact surface. Vertex
// fields use z * heightmap.resolution + x. Bedrock is top elevation (not depth),
// soil is thickness, and height = bedrock + soil, all in world units.
struct Fields {
    HeightmapGenerator::Heightmap heightmap;
    int playableResolution = 0;
    float playableWorldSize = 0;
    int apronCells = 0;
    float spacing = 0;
    std::vector<float> bedrock;
    std::vector<float> soil;
    std::vector<float> erodibility; // dimensionless [0,1]; larger = less resistant
    // Outward-facing boundary flags only; all exterior faces are open.
    // No interior drain, edge wall, forced water level or carved outlet.
    // The solver must account for water/solid exports across these faces.
    std::vector<uint8_t> openFaces;

    HeightmapGenerator::Heightmap crop() const;
    size_t payloadBytes() const;
};

Fields generate(int playableResolution, float worldSize, uint32_t seed, const Settings& settings);

} // namespace MacroTerrain
