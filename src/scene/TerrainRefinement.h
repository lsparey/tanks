#pragma once

#include "MacroTerrain.h"
#include <string_view>

namespace TerrainRefinement {
inline constexpr uint32_t kVersion = 1;
struct Result {
    int sourceResolution = 0, targetResolution = 0; // full domain, including apron
    double elapsedMs = 0;
    // Reconstruction is geometric resampling, not simulated erosion. Report
    // its separate dual-cell volume changes rather than altering solver budgets.
    double soilVolumeDelta = 0, bedrockVolumeDelta = 0;
};

// Parse the shared game/probe option: off=0, on/2x=1, 4x=2 passes.
int parsePasses(std::string_view);

// Double the intervals once or twice over the SAME physical domain. Retain coarse vertices,
// reconstruct bounded cubic midpoints and double the apron cells. Must run
// after settlement and before drainage, water or contact/render construction.
// Failures leave the input intact. Erosion diagnostics remain on their source grid.
Result apply(MacroTerrain::Fields&, int passes = 1);
}
