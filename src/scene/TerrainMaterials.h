#pragma once

#include "HydraulicErosion.h"
#include "TerrainWater.h"

namespace TerrainMaterials {

inline constexpr uint32_t kVersion = 1;
struct Settings {
    // Bounded artistic references converting generated quantities into [0,1]
    // classifications. World units / solver field units; independent of grid
    // resolution and refinement.
    float soilRockThreshold = .02f;   // soil thickness fully reading as rock
    float soilGrassThreshold = .2f;   // soil thickness fully covering rock
    // Thin soil only reads as rock where the ground is also steep or freshly
    // scarred; flat or gently sloped thin-soil uplands stay grassed pasture.
    // British summer hills show rock sparingly: scarps and cut banks only.
    float slopeRockLow = .35f, slopeRockHigh = .8f;      // rise over run
    float erosionScarLow = .28f, erosionScarHigh = .42f; // cumulative hydraulic cut
    float depositionLow = .25f, depositionHigh = .9f;    // cumulative deposit depth
    float exposureLow = 8, exposureHigh = 18;            // storm depth-seconds
    float throughflowLow = 30, throughflowHigh = 80;     // cumulative volume/area
    float bankMoistureDistance = 3.5f;                   // world units from final water
    float stormMoisture = .35f, gullyMoisture = .5f;     // bounded contribution weights
};
struct Fields {
    int resolution = 0;   // playable samples per side, matching the final surface
    float worldSize = 0;  // playable world size
    std::vector<float> rock, moisture, sediment; // [0,1] playable vertex fields
    // Bilinear samples on the playable grid; finite coordinates are clamped to
    // the domain. These are material classifications, not contact geometry.
    float rockAt(float x, float z) const;
    float moistureAt(float x, float z) const;
    float sedimentAt(float x, float z) const;
    double elapsedMs = 0;
    size_t payloadBytes() const;
};

// Read-only classification of the FINAL ground for materials and placement.
// Rock follows thin/stripped soil and strong hydraulic scars; moisture follows
// distance to final standing/moving water, storm water exposure and
// concentrated throughflow; sediment follows cumulative deposition. Erosion
// diagnostics stay on their coarse simulation grid and are sampled bilinearly
// under any refined final surface. This stage never alters terrain or water.
Fields build(const MacroTerrain::Fields& fields, const HydraulicErosion::Result& erosion,
             const TerrainDrainage::Result& drainage, const LakeWater::Result& lakes,
             const TerrainWater::Result& water, const Settings& settings = {});
void validate(const Settings& settings);

} // namespace TerrainMaterials
