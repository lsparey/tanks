#pragma once

#include <cstdint>
#include <vector>

#include "HeightmapGenerator.h"
#include "TerrainSurface.h"

// Vulkan-free flood-water analysis over a bare heightmap -- the same
// algorithm WaterGenerator::computeFloodField runs for legacy terrain's
// rendered water, but kept here (in the Vulkan-free `terrain_generation`
// library) so TerrainSelection's legacy seed search can run it too, before
// any GPU Terrain/mesh object exists. WaterGenerator::FloodField is a type
// alias for HeightmapFlood::Field; WaterGenerator::computeFloodField
// delegates to compute() below.
namespace HeightmapFlood {
struct Field {
    int resolution = 0;
    float worldSize = 0.0f;
    float maxDepth = 1.0f;         // as passed to compute(); used to normalize color depth
    std::vector<bool> submerged;   // true only for cells actually underwater
    std::vector<float> waterLevel; // meaningful only where submerged[i] is true
};

// Connected low-lying basins (4-connected flood fill wherever height <
// threshold) each get their own flat water level, capped to at most
// maxDepth above that basin's own floor -- see WaterGenerator's own class
// comment for the full rationale. This is the exact algorithm formerly in
// WaterGenerator::computeFloodField, taking the heightmap directly instead
// of a GPU Terrain wrapper.
Field compute(const HeightmapGenerator::Heightmap&, float threshold, float maxDepth);

// TerrainPlayability::analyze()'s per-triangle water test for a legacy
// heightmap: true if any of the triangle's 3 corner vertices is submerged
// in `field`. Mirrors TerrainWater::Surface::triangleHasWater's "any wet
// corner" shape; `triangle` is `2*quad+t` as in TerrainSurface::quadIndices.
// Throws std::invalid_argument if field.resolution doesn't match ground's.
bool triangleHasWater(const Field&, const TerrainSurface& ground, uint32_t triangle);
} // namespace HeightmapFlood
