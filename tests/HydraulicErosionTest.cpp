#include "scene/TerrainGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void close(double a, double b, double tolerance, const char* message) {
    require(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance, message);
}
template<class F> void rejects(F&& f) {
    bool rejected = false;
    try { f(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "invalid erosion request was accepted");
}
MacroTerrain::Fields fixture(int n, bool open, int shape = 0, double soilDepth = .5) {
    MacroTerrain::Fields f;
    f.playableResolution = n;
    f.playableWorldSize = 8;
    f.spacing = 8.0f / (n - 1);
    f.heightmap = {n, 8, std::vector<float>(size_t(n) * n)};
    f.bedrock.resize(size_t(n) * n);
    f.soil.resize(size_t(n) * n, float(soilDepth));
    f.erodibility.resize(size_t(n) * n, 1);
    f.openFaces.resize(size_t(n) * n);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            double px = x * f.spacing - 4, pz = z * f.spacing - 4;
            double height = shape == 1 ? .1 * (4 - px) : shape == 2 ? .1 * (4 - pz) :
                shape == 3 ? .03 * (px * px + pz * pz) : shape == 4 ? .4 - .1 * std::abs(px) :
                shape == 5 ? .04 * std::pow(std::abs(px) - .4 * std::max(0.0, -pz), 2) - .08 * pz : 0;
            size_t i = size_t(z) * n + x;
            f.bedrock[i] = float(height - soilDepth);
            f.heightmap.heights[i] = f.bedrock[i] + f.soil[i];
            if (open) f.openFaces[i] = (x == 0 ? 1 : 0) | (x == n - 1 ? 2 : 0) |
                                      (z == 0 ? 4 : 0) | (z == n - 1 ? 8 : 0);
        }
    }
    return f;
}
void check(const MacroTerrain::Fields& fields, const HydraulicErosion::Result& r) {
    const auto& b = r.budget;
    close(b.waterResidual, 0, 1e-8 * (1 + b.initialWater + b.rainfall), "water budget drift");
    close(b.solidResidual - b.solidRoundingDelta, 0,
          1e-8 * (1 + b.initialSoil + b.initialSediment + b.convertedBedrock), "solid transport budget drift");
    close(b.solidResidual, 0, 2e-5 * (1 + b.initialSoil + b.initialSediment + b.convertedBedrock),
          "excessive terrain quantization loss");
    for (size_t i = 0; i < r.water.size(); ++i) {
        require(std::isfinite(r.water[i] + r.sediment[i] + r.erosion[i] + r.deposition[i] +
                              r.waterExposure[i] + r.throughflow[i] + r.relaxation[i]) &&
                r.water[i] >= 0 && r.sediment[i] >= 0 && r.erosion[i] >= 0 && r.deposition[i] >= 0,
                "invalid erosion diagnostic");
        require(fields.soil[i] >= 0 && std::isfinite(fields.heightmap.heights[i]) &&
                fields.heightmap.heights[i] == fields.bedrock[i] + fields.soil[i], "invalid final ground");
    }
}
}

int main() {
    HydraulicErosion::Settings settings;
    settings.duration = 2;
    settings.rainDuration = 1;
    settings.rainfall = .04;
    settings.infiltration = .01;
    settings.evaporation = .005;
    settings.talusPasses = 0;
    auto flat = fixture(9, false);
    auto result = HydraulicErosion::run(flat, settings);
    check(flat, result);
    close(result.budget.rainfall, 2.56, 1e-10, "rain must integrate to actual domain area");
    close(result.budget.finalWater, .64, 1e-10, "flat closed fixture water balance");
    require(result.budget.exportedWater == 0 && result.budget.exportedSediment == 0, "closed fixture leaked");
    for (double h : result.water) close(h, .01, 1e-10, "flat water lost symmetry");

    auto dry = fixture(9, false);
    auto drying = settings;
    drying.duration = .1; drying.rainfall = 0; drying.infiltration = 0; drying.evaporation = 1;
    HydraulicErosion::InitialState wetSediment{std::vector<double>(81, .01), std::vector<double>(81, .1)};
    auto settled = HydraulicErosion::run(dry, drying, wetSediment);
    check(dry, settled);
    for (size_t i = 0; i < 81; ++i) {
        require(settled.water[i] == 0 && settled.sediment[i] == 0, "drying discarded or stranded sediment");
        close(dry.soil[i], .6, 1e-7, "drying did not deposit sediment");
    }

    settings.infiltration = 0; settings.evaporation = 0;
    HydraulicErosion::InitialState initial{std::vector<double>(289, .08), std::vector<double>(289, .02)};
    for (bool open : {false, true}) {
        // Slope, bowl/spill edge, dividing ridge and converging valley fixture.
        for (int shape : {1, 3, 4, 5}) {
            auto field = fixture(17, open, shape);
            auto r = HydraulicErosion::run(field, settings, initial);
            check(field, r);
            if (open) require(r.budget.exportedWater > 0 && r.budget.exportedSediment > 0, "open fixture did not export");
            else require(r.budget.exportedWater == 0 && r.budget.exportedSediment == 0, "closed slope leaked");
        }
    }
    auto east = fixture(17, true, 1), south = fixture(17, true, 2);
    // A supplied bowl with one low spill saddle and otherwise closed rim.
    auto bowl = fixture(17, false, 3);
    HydraulicErosion::InitialState lake{std::vector<double>(289), {}};
    for (int z = 0; z < 17; ++z) for (int x = 0; x < 17; ++x) {
        size_t i = size_t(z) * 17 + x;
        if (x == 0 || x == 16 || z == 0 || z == 16) bowl.bedrock[i] = .8f - bowl.soil[i];
        if (x == 16 && z == 8) { bowl.bedrock[i] = .25f - bowl.soil[i]; bowl.openFaces[i] = MacroTerrain::PositiveX; }
        bowl.heightmap.heights[i] = bowl.bedrock[i] + bowl.soil[i];
        lake.water[i] = std::max(0.0, .55 - bowl.heightmap.heights[i]);
    }
    auto spillSettings = settings;
    spillSettings.rainfall = 0; spillSettings.erosionRate = 0; spillSettings.bedrockRate = 0;
    auto spilled = HydraulicErosion::run(bowl, spillSettings, lake);
    check(bowl, spilled);
    require(spilled.budget.exportedWater > 0 && spilled.budget.finalWater < spilled.budget.initialWater,
            "supplied bowl did not spill through its outlet");

    auto a = HydraulicErosion::run(east, settings, initial);
    auto b = HydraulicErosion::run(south, settings, initial);
    for (int z = 0; z < 17; ++z) for (int x = 0; x < 17; ++x) {
        close(a.water[z * 17 + x], b.water[x * 17 + z], 1e-8, "axis-swap bias in water");
        close(east.heightmap.heights[z * 17 + x], south.heightmap.heights[x * 17 + z], 1e-6, "axis-swap bias in erosion");
    }
    auto halfStep = settings; halfStep.maxTimestep *= .5;
    auto stepField = fixture(17, true, 1);
    auto half = HydraulicErosion::run(stepField, halfStep, initial);
    close(a.budget.exportedWater, half.budget.exportedWater, a.budget.exportedWater * .08, "timestep convergence regressed");
    auto fineField = fixture(33, true, 1);
    HydraulicErosion::InitialState fineInitial{std::vector<double>(1089, .08), std::vector<double>(1089, .02)};
    auto fine = HydraulicErosion::run(fineField, settings, fineInitial);
    check(fineField, fine);
    close(a.budget.exportedWater, fine.budget.exportedWater, a.budget.exportedWater * .15, "spatial convergence regressed");

    auto bare = fixture(17, true, 1, 0), resistant = bare;
    std::fill(resistant.erodibility.begin(), resistant.erodibility.end(), 0);
    HydraulicErosion::InitialState clearWater{std::vector<double>(289, .08), {}};
    auto erodedRock = HydraulicErosion::run(bare, settings, clearWater);
    auto hardRock = HydraulicErosion::run(resistant, settings, clearWater);
    check(bare, erodedRock); check(resistant, hardRock);
    require(erodedRock.budget.convertedBedrock > 0 && hardRock.budget.convertedBedrock == 0, "bedrock resistance ignored");
    for (size_t i = 0; i < bare.bedrock.size(); ++i)
        require(resistant.bedrock[i] - bare.bedrock[i] <= settings.bedrockRate * settings.duration + 1e-6,
                "bedrock rate limit exceeded");

    auto spike = fixture(9, false, 0, 0);
    spike.soil[40] = spike.heightmap.heights[40] = 2;
    auto relax = settings; relax.duration = 0; relax.talusPasses = 8;
    auto relaxed = HydraulicErosion::run(spike, relax);
    check(spike, relaxed);
    require(spike.soil[40] < 2 && relaxed.budget.convertedBedrock == 0, "talus did not move only loose soil");

    auto checker = fixture(9, false, 0, 0);
    for (int z = 0; z < 9; ++z) for (int x = 0; x < 9; ++x)
        checker.soil[z * 9 + x] = checker.heightmap.heights[z * 9 + x] = (x + z) % 2 ? .8f : .2f;
    HydraulicErosion::InitialState checkerWater{std::vector<double>(81, .1), {}};
    auto checkerResult = HydraulicErosion::run(checker, settings, checkerWater);
    check(checker, checkerResult);
    require(*std::min_element(checker.heightmap.heights.begin(), checker.heightmap.heights.end()) >= .2f,
            "erosion excavated new checkerboard pits");
    require(*std::max_element(checkerResult.erosion.begin(), checkerResult.erosion.end()) > 0,
            "face slopes failed to detect checkerboard crests");

    // Unequal incoming pressure drives water through a local bed minimum.
    // Uphill face slopes must not give this minimum carrying capacity and
    // leave its load suspended: that feedback produced alternating deposits.
    auto pit = fixture(9, false, 1);
    pit.bedrock[40] = .05f - pit.soil[40];
    pit.heightmap.heights[40] = pit.bedrock[40] + pit.soil[40];
    HydraulicErosion::InitialState pitWater{std::vector<double>(81, .1), std::vector<double>(81, 0)};
    pitWater.sediment[40] = .03;
    auto oneStep = settings; oneStep.duration = .05; oneStep.rainfall = 0;
    auto filled = HydraulicErosion::run(pit, oneStep, pitWater);
    check(pit, filled);
    require(filled.deposition[40] > .001, "a local bed minimum failed to collect sediment");

    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        TerrainGenerator::Settings s;
        s.preset = TerrainGenerator::Preset::ErodedValley; s.seed = seed; s.resolution = 33;
        s.erosion.duration = 2; s.erosion.rainDuration = .5;
        auto first = TerrainGenerator::build(s), repeat = TerrainGenerator::build(s);
        check(*first.generationFields, *first.erosion);
        require(first.surface.heightmap().heights == first.generationFields->crop().heights, "mesh was cropped before erosion");
        require(first.surface.heightmap().heights == repeat.surface.heightmap().heights &&
                first.erosion->water == repeat.erosion->water && first.erosion->sediment == repeat.erosion->sediment &&
                first.erosion->steps == repeat.erosion->steps, "non-deterministic erosion");
        s.erosion.workers = 4;
        auto parallel = TerrainGenerator::build(s);
        require(first.surface.heightmap().heights == parallel.surface.heightmap().heights &&
                first.erosion->water == parallel.erosion->water && first.erosion->sediment == parallel.erosion->sediment &&
                first.erosion->erosion == parallel.erosion->erosion && first.erosion->deposition == parallel.erosion->deposition &&
                first.erosion->relaxation == parallel.erosion->relaxation &&
                first.erosion->budget.waterResidual == parallel.erosion->budget.waterResidual &&
                first.erosion->budget.solidResidual == parallel.erosion->budget.solidResidual &&
                first.erosion->steps == parallel.erosion->steps, "worker count changed erosion");
        for (size_t i = 0; i < first.erosion->water.size(); ++i)
            require(first.erosion->sediment[i] <= s.erosion.maxConcentration * first.erosion->water[i] + 1e-10,
                    "suspended sediment exceeded concentration cap");
        if (seed == 7331) for (int workers : {2, 3}) {
            s.erosion.workers = workers;
            auto other = TerrainGenerator::build(s);
            require(first.surface.heightmap().heights == other.surface.heightmap().heights &&
                    first.erosion->water == other.erosion->water && first.erosion->sediment == other.erosion->sediment,
                    "intermediate worker count changed erosion");
        }
    }
    auto unchanged = fixture(9, false);
    auto original = unchanged.heightmap.heights;
    auto tooFew = settings; tooFew.maxSteps = 1; tooFew.workers = 4;
    rejects([&] { HydraulicErosion::run(unchanged, tooFew); });
    require(unchanged.heightmap.heights == original, "failed erosion mutated the terrain");
    auto bad = settings; bad.maxTimestep = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { HydraulicErosion::run(unchanged, bad); });
    HydraulicErosion::InitialState negative{{-1}, {}};
    rejects([&] { HydraulicErosion::run(unchanged, settings, negative); });
    auto wrongSize = unchanged; wrongSize.heightmap.worldSize = 10;
    rejects([&] { HydraulicErosion::run(wrongSize, settings); });
    unchanged.openFaces[40] = 1;
    rejects([&] { HydraulicErosion::run(unchanged, settings); });
}
