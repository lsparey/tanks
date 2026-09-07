#pragma once

#include "StreamNetwork.h"

namespace ChannelCarving {
inline constexpr uint32_t kVersion = 1;
struct Settings {
    float maximumCut = .6f; // world units; no fill or hidden sediment deposition
};
struct Budget {
    double initialSoil = 0, finalSoil = 0;
    double exportedSoil = 0, exportedBedrock = 0;
    double removedGround = 0, materialResidual = 0, surfaceRoundingDelta = 0;
};
struct Result {
    std::vector<float> cutDepth; // full domain; actual final ground reduction
    std::vector<uint8_t> protectedCells; // basin vertices and their mesh-edge neighbours
    Budget budget;
    uint32_t changedCells = 0;
    double elapsedMs = 0;
    size_t payloadBytes() const;
};

// One bounded soil-first excavation pass using PRELIMINARY drainage/streams.
// Carves parabolic sections at requested widths/depths below the old ground
// centreline, subject to downstream bed constraints and protected lake rims.
// Validates and prepares all changes before committing. Caller MUST rebuild
// drainage, lakes, streams and contact/render surfaces after this height change.
Result apply(MacroTerrain::Fields&, const TerrainDrainage::Result&,
             const StreamNetwork::Result&, const Settings& = {});
void validate(const Settings&);
} // namespace ChannelCarving
