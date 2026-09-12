#pragma once

#include "MacroTerrain.h"

namespace RockOutcrops {

inline constexpr uint32_t kVersion = 1;
struct Settings {
    // Slope gate (rise over run) for exposure: below low nothing changes,
    // above high the full effect applies. Soil thicker than soilCover fully
    // suppresses exposure, so valley floors and pasture stay untouched.
    float slopeLow = .38f, slopeHigh = .75f;
    float soilCover = .45f;
    float strataHeight = 1.1f;   // vertical ledge spacing of terraced bedrock
    float terraceStrength = .85f; // how strongly exposed faces snap to ledges
    float reliefAmplitude = .7f;  // ridged crest/hollow displacement bound
    float maximumChange = 1.2f;   // hard bound on |height change|, world units
    float soilStripping = .85f;   // fraction of soil removed at full exposure
};
struct Result {
    uint32_t changedCells = 0;
    // Signed dual-cell volume deltas of this artistic geology pass. These are
    // deliberate authored changes, accounted rather than conserved.
    double bedrockVolumeDelta = 0, soilVolumeDelta = 0;
    double elapsedMs = 0;
};

// Bounded artistic geology detail: steep, thin-soiled ground gains terraced
// strata ledges and ridged crests, and its soil thins so downstream material
// classification reads the faces as rock. Seeded and deterministic. Runs on
// the FINAL grid after settlement/refinement and MUST precede drainage and
// every other water/contact consumer, which are rebuilt on the new ground.
Result apply(MacroTerrain::Fields& fields, uint32_t seed, const Settings& settings = {});
void validate(const Settings& settings);

} // namespace RockOutcrops
