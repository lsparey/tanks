#include "scene/TerrainGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid material request accepted");
}
MacroTerrain::Fields field(int n = 9) {
    MacroTerrain::Fields f;
    f.playableResolution = n; f.playableWorldSize = n - 1; f.spacing = 1;
    f.heightmap = {n, float(n - 1), std::vector<float>(size_t(n) * n)};
    f.soil.assign(size_t(n) * n, .5f);
    f.bedrock.assign(size_t(n) * n, -.5f);
    f.erodibility.assign(size_t(n) * n, .5f);
    f.openFaces.resize(size_t(n) * n); f.openFaces[size_t(n / 2) * n] = MacroTerrain::NegativeX;
    return f;
}
HydraulicErosion::Result diagnostics(int coarse) {
    HydraulicErosion::Result e;
    size_t count = size_t(coarse) * coarse;
    e.erosion.assign(count, 0); e.deposition.assign(count, 0);
    e.waterExposure.assign(count, 0); e.throughflow.assign(count, 0);
    e.water.assign(count, 0); e.sediment.assign(count, 0); e.relaxation.assign(count, 0);
    e.finalized = true;
    return e;
}
void bounded(const TerrainMaterials::Fields& m) {
    require(m.resolution >= 2 && m.rock.size() == size_t(m.resolution) * m.resolution &&
            m.moisture.size() == m.rock.size() && m.sediment.size() == m.rock.size(),
            "material field dimensions disagree");
    for (const auto* values : {&m.rock, &m.moisture, &m.sediment})
        for (float v : *values)
            require(std::isfinite(v) && v >= 0 && v <= 1, "material classification left [0,1]");
}
void same(const TerrainMaterials::Fields& a, const TerrainMaterials::Fields& b) {
    require(a.resolution == b.resolution && a.rock == b.rock && a.moisture == b.moisture &&
            a.sediment == b.sediment, "nondeterministic material fields");
}
}

int main() {
    // Two-tier pond: an under-supplied partial lake at .5 with wet and dry
    // stream entries, steep pit walls and a flat surrounding plain.
    auto f = field();
    std::fill(f.heightmap.heights.begin(), f.heightmap.heights.end(), 5);
    for (int x = 0; x <= 3; ++x) f.heightmap.heights[4 * 9 + x] = x == 0 ? 0 : 1;
    f.heightmap.heights[4 * 9 + 6] = 2;
    for (int z = 3; z <= 5; ++z) {
        f.heightmap.heights[z * 9 + 4] = 0;
        f.heightmap.heights[z * 9 + 5] = .5f;
    }
    for (size_t i = 0; i < f.heightmap.heights.size(); ++i) {
        f.bedrock[i] = f.heightmap.heights[i] - f.soil[i];
    }
    // Thin soil along the northern rim of the pit; thick soil elsewhere.
    f.soil[2 * 9 + 4] = .01f;
    f.bedrock[2 * 9 + 4] = f.heightmap.heights[2 * 9 + 4] - .01f;
    auto d = TerrainDrainage::analyze(f);
    auto w = LakeWater::build(f, d);
    // High enough that only the main groove channel is selected; a tiny
    // threshold would select rills all over the plain and wet every corner.
    StreamNetwork::Settings streams; streams.minimumDischarge = .5;
    auto n = StreamNetwork::build(f, d, w, streams);
    auto cw = TerrainWater::build(f, d, w, n);
    auto e = diagnostics(9);
    // A strong deposit and a strong scar in opposite corners, away from water.
    e.deposition[1 * 9 + 7] = 2;
    e.erosion[7 * 9 + 7] = 1;
    auto m = TerrainMaterials::build(f, e, d, w, cw, {});
    bounded(m);
    same(m, TerrainMaterials::build(f, e, d, w, cw, {}));

    // Bank moisture is saturated on the lake and decays to zero beyond the
    // configured distance; the far corner has no exposure or throughflow.
    require(m.moistureAt(0, 0) == 1, "standing water is not saturated moisture");
    require(m.moistureAt(-4, -4) == 0, "distant dry ground acquired moisture");
    require(m.moistureAt(1.5f, 0) > 0 && m.moistureAt(1.5f, 0) < 1,
            "bank moisture does not decay with distance");

    // Steep thin-soil pit wall reads as rock; flat thick-soil plain does not.
    require(m.rockAt(0, -2) > .5f, "steep thin-soil wall is not rock");
    require(m.rockAt(-3.5f, 3.5f) == 0, "flat thick-soil plain reads as rock");
    // The strong scar reads as rock even under thicker soil only if soil is
    // thin; with .5 soil it stays covered.
    require(m.rockAt(3, 3) < .5f, "covered scar ignored its soil");

    // Deposition classifies as sediment where recorded and nowhere else.
    require(m.sedimentAt(3, -3) == 1, "strong deposit is not sediment");
    require(m.sedimentAt(-3, 3) == 0, "clean ground reads as sediment");

    // Refined final grids sample coarse erosion diagnostics bilinearly.
    auto coarse = diagnostics(5);
    std::fill(coarse.deposition.begin(), coarse.deposition.end(), 2.0);
    auto refined = TerrainMaterials::build(f, coarse, d, w, cw, {});
    bounded(refined);
    require(refined.sedimentAt(0, 0) == 1, "coarse diagnostics were not resampled");

    // Samplers clamp finite outside coordinates and reject non-finite ones.
    require(m.rockAt(100, 100) == m.rockAt(4, 4), "outside sample not clamped");
    rejects([&] { m.rockAt(std::numeric_limits<float>::quiet_NaN(), 0); });

    for (int bad = 0; bad < 6; ++bad) {
        auto ff = f; auto ee = e; TerrainMaterials::Settings s;
        if (bad == 0) ee.erosion.pop_back();
        if (bad == 1) ee.finalized = false;
        if (bad == 2) ee = diagnostics(6); // (9-1) % (6-1) != 0
        if (bad == 3) s.soilRockThreshold = s.soilGrassThreshold;
        if (bad == 4) s.bankMoistureDistance = 0;
        if (bad == 5) s.exposureLow = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { TerrainMaterials::build(ff, ee, d, w, cw, s); });
    }

    // Generator integration: fields require combined water, stay bounded and
    // deterministic across worker counts, and report their cost.
    TerrainGenerator::Settings s;
    s.materials.emplace();
    rejects([&] { TerrainGenerator::build(s); });
    s.preset = TerrainGenerator::Preset::DrainedValley;
    s.lakes.emplace(); s.streams.emplace(); s.combinedWater = true;
    s.resolution = 33; s.erosion.duration = .25; s.erosion.rainDuration = .15; s.erosion.talusPasses = 1;
    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        s.seed = seed; s.erosion.workers = 1;
        auto a = TerrainGenerator::build(s);
        require(a.materials.has_value() && a.statistics.materialsBytes > 0, "generator omitted material fields");
        require(a.materials->resolution == 33 && a.materials->worldSize == s.worldSize,
                "material fields do not match the playable surface");
        bounded(*a.materials);
        s.erosion.workers = 4;
        auto b = TerrainGenerator::build(s);
        same(*a.materials, *b.materials);
    }
}
