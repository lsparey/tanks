#pragma once

#include "MacroTerrain.h"

namespace TerrainDrainage {

struct Settings {
    // Steady depth/time proxies, separate from erosion duration and remaining
    // simulation water. Accumulated runoff assumes every depression overflows;
    // it is potential supply, NOT a permanent-water depth or discharge model.
    double rainfall = .025;
    double infiltration = .002;
};

struct Basin {
    float spillElevation = 0;
    float minimumGround = 0;
    double area = 0;
    double storageToSpill = 0; // dual-cell volume, not exact triangle integral
    uint32_t cells = 0;
    uint32_t outletLinks = 0; // routing edges leaving this depression component
    int32_t spillFrom = -1, spillTo = -1; // first such edge in routing order
};

struct Result {
    // Full domain, including apron. Nothing here modifies authoritative ground.
    // Neighbours are the six actual terrain mesh edges (cardinals + NW/SE).
    std::vector<float> spillElevation;
    std::vector<int32_t> downstream; // -1 only at designated open boundary faces
    std::vector<uint32_t> order; // downstream precedes upstream, including flats
    std::vector<uint32_t> outlet; // terminal boundary vertex / watershed label
    std::vector<double> contributingArea, runoff;
    // Connected strictly submerged components at their EXTERNAL escape level.
    // Equal-height saddles separate components. This is not a hierarchy of
    // nested sub-basins for partially filled lakes.
    std::vector<int32_t> basin; // -1 outside depressions
    std::vector<Basin> basins;
    double domainArea = 0, generatedRunoff = 0, outletRunoff = 0, runoffResidual = 0;
    double elapsedMs = 0;
    size_t peakWorkingBytes = 0; // vector payload incl. result, excl. caller fields
    size_t payloadBytes() const;
};

// Priority-Flood analysis with deterministic ties and acyclic flat routing.
// Requires at least one explicit exterior outlet; closed domains are rejected
// because they have no finite external spill level. Run after final settlement
// or any other height change, before cropping to the playable area.
Result analyze(const MacroTerrain::Fields& fields, const Settings& settings = {});

} // namespace TerrainDrainage
