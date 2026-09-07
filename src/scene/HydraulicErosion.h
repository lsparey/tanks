#pragma once

#include <array>
#include "MacroTerrain.h"

namespace HydraulicErosion {

struct Settings {
    double duration = 24;       // artistic simulation seconds, not geological time
    double rainDuration = 18;
    double maxTimestep = .05;
    double cfl = .4;
    int maxSteps = 4096;        // deterministic failure bound, never a wall-clock cutoff
    double rainfall = .025;    // water depth / second
    double infiltration = .002;
    double evaporation = .001;
    double drag = .8;          // pipe momentum damping / second
    double capacity = 25;      // seconds / world unit; capacity = k*speed*depth*slope
    double maxConcentration = .5; // maximum suspended solid volume / water volume
    double erosionRate = .5;   // approach to capacity / second
    double depositionRate = 1;
    double bedrockRate = .006; // maximum rock lowering / second, scaled by erodibility
    double maxChangeRate = .2; // ordinary exchange depth / second; settlement is separate
    int talusPasses = 8;
    double talusSlope = .7;
    double talusFraction = .15; // fraction of excess neighbour height exchanged per pass
    double maxTalusDepth = .02;
    int workers = 1; // serial reference or bounded 2..4 CPU workers
};

struct InitialState {
    // Optional vertex depth fields; empty means zero. Suspended sediment uses
    // equivalent solid thickness, assuming constant solid density.
    std::vector<double> water;
    std::vector<double> sediment;
};
struct Budget {
    // World-unit cubed volumes, integrated over vertex-centred dual cells.
    // Edge cells have half width and corner cells quarter area, so total area
    // equals the square's actual worldSize squared.
    double initialWater = 0, rainfall = 0, infiltration = 0, evaporation = 0;
    double exportedWater = 0, finalWater = 0, waterResidual = 0;
    double removedTransientWater = 0; // explicit terminal reset, NOT boundary export
    double initialSoil = 0, initialSediment = 0, convertedBedrock = 0;
    double exportedSediment = 0, finalSoil = 0, finalSediment = 0;
    double solidResidual = 0; // final + exports - initial - converted bedrock
    double solidRoundingDelta = 0; // explicit double -> float terrain quantization
    double settledSediment = 0; // terminal transfer into soil, already in finalSoil
};
struct Result {
    // Full-domain vertex fields. Water is transient simulation water, NOT the
    // persistent rivers/lakes of the finished level. Scratch fluxes are freed.
    std::vector<double> water, sediment;
    std::vector<double> erosion, deposition; // cumulative hydraulic depths
    std::vector<double> waterExposure;       // integral depth * seconds
    std::vector<double> throughflow;         // cumulative outgoing volume / area
    std::vector<double> relaxation;          // net soil depth change from talus
    Budget budget;
    int steps = 0;
    double simulatedSeconds = 0, elapsedMs = 0;
    size_t peakWorkingBytes = 0; // solver vector payload incl. result, excl. caller fields
    bool finalized = false;
    double settlementMs = 0;
    size_t payloadBytes() const;
};

// Conservative CPU reference. Changes fields only after successful completion.
// Run over the FULL domain before cropping or recomputing final drainage.
Result run(MacroTerrain::Fields& fields, const Settings& settings = {}, const InitialState& initial = {});

// Last height-changing pass before drainage: deposit all suspended solids in
// place and explicitly remove temporary simulation water. Updates diagnostics
// and budgets, preserving bedrock. Both inputs must be the matching output of
// run(); failures leave them intact. A second call is rejected.
void settle(MacroTerrain::Fields& fields, Result& result);

} // namespace HydraulicErosion
