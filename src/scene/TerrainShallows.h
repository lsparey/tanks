#pragma once

#include "TerrainDrainage.h"
#include "LakeWater.h"

namespace TerrainShallows {
struct Result {
    double addedSoil = 0, elapsedMs = 0;
    uint32_t removedLakes = 0;
};
// Raise only deep depression floors, retaining rims and escape elevations.
// Reserve stream headroom separately from this maximum standing depth.
// This is imported artistic fill, not erosion sediment. Rebuild hydrology
// afterwards, before constructing any final render/contact surfaces.
Result apply(MacroTerrain::Fields&, const TerrainDrainage::Result&, float maximumStandingDepth);
// Fill small or narrow standing-water basins to their spill, removing their
// physical depressions as well as their water. A retained lake must meet the
// area threshold and contain a disk of minimumRadius in world units.
Result removeSmallLakes(MacroTerrain::Fields&, const TerrainDrainage::Result&,
                        const LakeWater::Result&, double minimumArea, float minimumRadius);
}
