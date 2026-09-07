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
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid drainage/settlement request accepted");
}
MacroTerrain::Fields fixture(int n) {
    MacroTerrain::Fields f;
    f.playableResolution = n;
    f.playableWorldSize = float(n - 1);
    f.spacing = 1;
    f.heightmap = {n, float(n - 1), std::vector<float>(size_t(n) * n, 0)};
    f.bedrock = f.heightmap.heights;
    f.soil.resize(size_t(n) * n, 0);
    f.erodibility.resize(size_t(n) * n, 1);
    f.openFaces.resize(size_t(n) * n, 0);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x)
            f.openFaces[size_t(z) * n + x] = (x == 0 ? 1 : 0) | (x == n - 1 ? 2 : 0) |
                (z == 0 ? 4 : 0) | (z == n - 1 ? 8 : 0);
    return f;
}
double area(const MacroTerrain::Fields& f, uint32_t i) {
    int n = f.heightmap.resolution, x = int(i % n), z = int(i / n);
    return double(f.spacing) * f.spacing * (x == 0 || x == n - 1 ? .5 : 1) *
        (z == 0 || z == n - 1 ? .5 : 1);
}
// Build reference adjacency from actual mesh triangles, independently of the
// drainage module's neighbour offsets. Also catches the wrong quad diagonal.
std::vector<std::vector<uint32_t>> adjacency(int n) {
    std::vector<std::vector<uint32_t>> edges(size_t(n) * n);
    for (int z = 0; z < n - 1; ++z) {
        for (int x = 0; x < n - 1; ++x) {
            auto indices = TerrainSurface::quadIndices(n, x, z);
            for (int t = 0; t < 6; t += 3) {
                for (int k = 0; k < 3; ++k) {
                    auto i = indices[t + k], j = indices[t + (k + 1) % 3];
                    edges[i].push_back(j);
                    edges[j].push_back(i);
                }
            }
        }
    }
    return edges;
}
void referenceSpills(const MacroTerrain::Fields& f, const TerrainDrainage::Result& r) {
    const auto& heights = f.heightmap.heights;
    auto edges = adjacency(f.heightmap.resolution);
    std::vector<float> spill(heights.size(), std::numeric_limits<float>::infinity());
    for (size_t i = 0; i < spill.size(); ++i) if (f.openFaces[i]) spill[i] = heights[i];
    // Bellman relaxation of the minimax path equation; deliberately no heap or
    // flood ordering, only used for small analytic fixtures.
    for (size_t pass = 0; pass < spill.size(); ++pass) {
        auto next = spill;
        for (size_t i = 0; i < spill.size(); ++i)
            for (auto j : edges[i]) next[i] = std::min(next[i], std::max(heights[i], spill[j]));
        if (next == spill) break;
        spill.swap(next);
    }
    require(spill == r.spillElevation, "priority flood differs from reference minimax escape levels");
}
void check(const MacroTerrain::Fields& f, const TerrainDrainage::Result& r, double effectiveRain = .023) {
    const size_t count = f.heightmap.heights.size();
    require(r.order.size() == count && r.downstream.size() == count && r.outlet.size() == count &&
            r.basin.size() == count && r.runoff.size() == count && r.contributingArea.size() == count,
            "drainage fields have different dimensions");
    std::vector<uint32_t> rank(count, uint32_t(count));
    for (size_t k = 0; k < count; ++k) {
        require(r.order[k] < count && rank[r.order[k]] == count, "routing order is not a permutation");
        rank[r.order[k]] = uint32_t(k);
    }
    auto edges = adjacency(f.heightmap.resolution);
    std::vector<double> upstreamArea(count), upstreamRunoff(count);
    std::vector<uint32_t> basinCells(r.basins.size(), 0), basinExits(r.basins.size(), 0);
    std::vector<double> basinStorage(r.basins.size(), 0), basinArea(r.basins.size(), 0);
    double outletArea = 0;
    for (uint32_t i = 0; i < count; ++i) {
        require(std::isfinite(r.spillElevation[i]) && r.spillElevation[i] >= f.heightmap.heights[i],
                "analysis lowered ground or contains invalid spill levels");
        int32_t j = r.downstream[i];
        require((j == -1) == (f.openFaces[i] != 0), "routing leaks through a closed boundary");
        if (j >= 0) {
            require(size_t(j) < count && rank[j] < rank[i], "cycle/upstream routing order");
            require(std::find(edges[i].begin(), edges[i].end(), uint32_t(j)) != edges[i].end(),
                    "route crosses a non-mesh diagonal");
            require(r.spillElevation[j] <= r.spillElevation[i], "routing climbs analysis surface");
            require(r.outlet[i] == r.outlet[j], "inconsistent watershed label");
            upstreamArea[j] += r.contributingArea[i];
            upstreamRunoff[j] += r.runoff[i];
        } else {
            require(r.outlet[i] == i, "outlet does not label itself");
            outletArea += r.contributingArea[i];
        }
        require(f.openFaces[r.outlet[i]] != 0, "watershed does not end at an outlet");
        int32_t b = r.basin[i];
        require((b >= 0) == (r.spillElevation[i] > f.heightmap.heights[i]), "wrong depression membership");
        if (b >= 0) {
            require(size_t(b) < r.basins.size() && r.basins[b].spillElevation == r.spillElevation[i],
                    "basin has inconsistent escape levels");
            ++basinCells[b];
            basinArea[b] += area(f, i);
            basinStorage[b] += (double(r.spillElevation[i]) - f.heightmap.heights[i]) * area(f, i);
            if (j < 0 || r.basin[j] != b) ++basinExits[b];
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        close(r.contributingArea[i], area(f, i) + upstreamArea[i], 1e-8, "confluence loses catchment area");
        close(r.runoff[i], effectiveRain * area(f, i) + upstreamRunoff[i], 1e-8, "confluence loses runoff");
    }
    for (size_t b = 0; b < r.basins.size(); ++b) {
        const auto& basin = r.basins[b];
        require(basin.cells == basinCells[b] && basin.outletLinks == basinExits[b] && basin.outletLinks > 0,
                "basin has wrong cells or no routing outlet");
        close(basin.area, basinArea[b], 1e-8, "wrong basin area");
        close(basin.storageToSpill, basinStorage[b], 1e-8, "wrong basin storage");
        require(basin.spillFrom >= 0 && r.basin[basin.spillFrom] == int32_t(b) &&
                r.downstream[basin.spillFrom] == basin.spillTo && basin.spillTo >= 0 &&
                r.basin[basin.spillTo] != int32_t(b), "invalid recorded spill edge");
    }
    close(outletArea, r.domainArea, 1e-8, "outlets lose domain area");
    close(r.domainArea, double(f.heightmap.worldSize) * f.heightmap.worldSize, 1e-6, "wrong dual-cell domain area");
    close(r.generatedRunoff, effectiveRain * r.domainArea, 1e-8, "incorrect rain/infiltration supply");
    close(r.runoffResidual, 0, 1e-8, "runoff budget drift");
    require(r.peakWorkingBytes >= r.payloadBytes(), "missing drainage memory accounting");
}
void same(const TerrainDrainage::Result& a, const TerrainDrainage::Result& b) {
    require(a.spillElevation == b.spillElevation && a.downstream == b.downstream && a.order == b.order &&
            a.outlet == b.outlet && a.contributingArea == b.contributingArea && a.runoff == b.runoff &&
            a.basin == b.basin && a.basins.size() == b.basins.size(), "nondeterministic drainage");
    for (size_t i = 0; i < a.basins.size(); ++i) {
        const auto& x = a.basins[i]; const auto& y = b.basins[i];
        require(x.spillElevation == y.spillElevation && x.minimumGround == y.minimumGround &&
                x.area == y.area && x.storageToSpill == y.storageToSpill && x.cells == y.cells &&
                x.outletLinks == y.outletLinks && x.spillFrom == y.spillFrom && x.spillTo == y.spillTo,
                "nondeterministic basin metadata");
    }
}
}

int main() {
    for (int shape = 0; shape < 5; ++shape) {
        auto f = fixture(9);
        for (int z = 0; z < 9; ++z) {
            for (int x = 0; x < 9; ++x) {
                float h = shape == 1 ? float(x) : shape == 2 ? float(std::abs(x - 4) + std::abs(z - 4)) :
                    shape == 3 ? float(4 - std::abs(x - 4)) :
                    shape == 4 ? float(((x * 17 + z * 31) ^ (x * z * 7)) % 11) : -100;
                f.heightmap.heights[size_t(z) * 9 + x] = h;
            }
        }
        auto original = f.heightmap.heights;
        auto r = TerrainDrainage::analyze(f);
        check(f, r); referenceSpills(f, r); same(r, TerrainDrainage::analyze(f));
        require(f.heightmap.heights == original, "drainage changed physical ground");
        if (shape == 0 || shape == 1 || shape == 3) require(r.basins.empty(), "flat/slope/ridge became a lake basin");
        auto dry = TerrainDrainage::analyze(f, {.001, .002});
        check(f, dry, 0);
        require(dry.generatedRunoff == 0 && dry.outletRunoff == 0, "infiltration excess creates negative runoff");
        require(dry.downstream == r.downstream && dry.spillElevation == r.spillElevation,
                "rainfall policy altered terrain routing");
    }

    // Bowl with a single two-unit sill. The rest of the boundary is closed;
    // low ground beyond it must not become an accidental escape route.
    auto bowl = fixture(7);
    std::fill(bowl.heightmap.heights.begin(), bowl.heightmap.heights.end(), 5);
    std::fill(bowl.openFaces.begin(), bowl.openFaces.end(), 0);
    for (int z = 2; z <= 4; ++z)
        for (int x = 2; x <= 4; ++x) bowl.heightmap.heights[size_t(z) * 7 + x] = 0;
    bowl.heightmap.heights[3 * 7 + 1] = 2;
    bowl.heightmap.heights[3 * 7] = 1;
    bowl.openFaces[3 * 7] = MacroTerrain::NegativeX;
    auto b = TerrainDrainage::analyze(bowl);
    check(bowl, b); referenceSpills(bowl, b);
    require(b.basins.size() == 1 && b.basins[0].cells == 9, "single-sill bowl incorrectly split");
    close(b.basins[0].spillElevation, 2, 0, "wrong bowl sill elevation");
    close(b.basins[0].storageToSpill, 18, 0, "wrong bowl capacity");
    close(b.contributingArea[3 * 7], 36, 0, "single outlet did not receive whole catchment");

    // Two lower pockets share an external spill level above their internal
    // saddle. They form one compound depression until that saddle reaches the
    // spill level, when strict submerged connectivity separates them.
    auto nested = fixture(9);
    std::fill(nested.heightmap.heights.begin(), nested.heightmap.heights.end(), 7);
    std::fill(nested.openFaces.begin(), nested.openFaces.end(), 0);
    nested.heightmap.heights[4] = 0;
    nested.openFaces[4] = MacroTerrain::NegativeZ;
    nested.heightmap.heights[9 + 4] = 5;
    for (int z = 2; z <= 6; ++z)
        for (int x = 2; x <= 6; ++x) nested.heightmap.heights[size_t(z) * 9 + x] = x == 4 ? 3 : 0;
    auto compound = TerrainDrainage::analyze(nested);
    check(nested, compound); referenceSpills(nested, compound);
    require(compound.basins.size() == 1 && compound.basins[0].cells == 25,
            "internal saddle incorrectly splits a fully connected depression");
    close(compound.basins[0].storageToSpill, 110, 0, "compound basin storage lost its saddle");
    for (int z = 2; z <= 6; ++z) nested.heightmap.heights[size_t(z) * 9 + 4] = 5;
    auto split = TerrainDrainage::analyze(nested);
    check(nested, split); referenceSpills(nested, split);
    require(split.basins.size() == 2 && split.basins[0].cells == 10 && split.basins[1].cells == 10,
            "dry spill-height saddle merged separate depression components");

    // A tempting NE diagonal is NOT a mesh edge. Escaping this pit must cross
    // a height-five vertex, despite the diagonally adjacent open low corner.
    auto diagonal = fixture(3);
    std::fill(diagonal.heightmap.heights.begin(), diagonal.heightmap.heights.end(), 5);
    diagonal.heightmap.heights[4] = diagonal.heightmap.heights[2] = 0;
    auto d = TerrainDrainage::analyze(diagonal);
    close(d.spillElevation[4], 5, 0, "drainage crossed the absent NE mesh diagonal");
    check(diagonal, d); referenceSpills(diagonal, d);

    // Controlled terminal settlement: the sill itself receives sediment. Final
    // drainage must observe its new height, with no unaccounted fluid/solids.
    bowl.bedrock = bowl.heightmap.heights;
    HydraulicErosion::Settings erosionSettings;
    erosionSettings.duration = 0; erosionSettings.talusPasses = 0;
    HydraulicErosion::InitialState initial;
    initial.water.resize(49, .3);
    initial.sediment.resize(49, .1);
    initial.sediment[3 * 7 + 1] = .5;
    auto erosion = HydraulicErosion::run(bowl, erosionSettings, initial);
    auto oldRock = bowl.bedrock;
    double oldWater = erosion.budget.finalWater, oldSediment = erosion.budget.finalSediment;
    HydraulicErosion::settle(bowl, erosion);
    require(erosion.finalized && bowl.bedrock == oldRock, "settlement changed rock or left unfinished state");
    require(erosion.budget.finalWater == 0 && erosion.budget.finalSediment == 0, "terminal fluid remains");
    close(erosion.budget.removedTransientWater, oldWater, 0, "temporary water reset not accounted");
    close(erosion.budget.settledSediment, oldSediment, 0, "settled sediment transfer not accounted");
    close(erosion.budget.waterResidual, 0, 1e-12, "terminal water budget drift");
    close(erosion.budget.solidResidual - erosion.budget.solidRoundingDelta, 0, 1e-12, "terminal solid budget drift");
    for (size_t i = 0; i < 49; ++i) {
        require(erosion.water[i] == 0 && erosion.sediment[i] == 0, "terminal fluid field not cleared");
        close(erosion.deposition[i], initial.sediment[i], 0, "terminal deposit missing from diagnostics");
        require(bowl.heightmap.heights[i] == bowl.bedrock[i] + bowl.soil[i], "final material/height mismatch");
    }
    auto after = TerrainDrainage::analyze(bowl);
    check(bowl, after); referenceSpills(bowl, after);
    close(after.basins[0].spillElevation, 2.5, 0, "drainage used pre-settlement sill");
    rejects([&] { HydraulicErosion::settle(bowl, erosion); });

    for (int invalid = 0; invalid < 4; ++invalid) {
        auto f = fixture(3);
        auto r = HydraulicErosion::run(f, erosionSettings, {std::vector<double>(9, .3), std::vector<double>(9, .1)});
        if (invalid == 0) r.water.back() = std::numeric_limits<double>::quiet_NaN();
        if (invalid == 1) r.sediment.pop_back();
        if (invalid == 2) r.budget.finalSoil = 12;
        if (invalid == 3) r.deposition.back() = -1;
        auto original = f.heightmap.heights;
        rejects([&] { HydraulicErosion::settle(f, r); });
        require(f.heightmap.heights == original && !r.finalized && r.deposition.front() == 0 &&
                f.soil.front() == 0 && r.sediment.front() == .1, "failed settlement partially committed");
    }
    for (int invalid = 0; invalid < 8; ++invalid) {
        auto f = fixture(3);
        TerrainDrainage::Settings s;
        if (invalid == 0) std::fill(f.openFaces.begin(), f.openFaces.end(), 0);
        if (invalid == 1) f.openFaces[4] = MacroTerrain::NegativeX;
        if (invalid == 2) f.openFaces[0] |= 128;
        if (invalid == 3) f.heightmap.heights.back() = std::numeric_limits<float>::infinity();
        if (invalid == 4) f.openFaces.pop_back();
        if (invalid == 5) f.spacing = 0;
        if (invalid == 6) s.rainfall = std::numeric_limits<double>::quiet_NaN();
        if (invalid == 7) s.infiltration = -1;
        rejects([&] { TerrainDrainage::analyze(f, s); });
    }

    // End-to-end fixed seeds at small resolution/duration keep this a fast CPU
    // test. Default-size/runtime evidence is collected separately by the probe.
    TerrainGenerator::Settings s;
    s.preset = TerrainGenerator::Preset::DrainedValley;
    s.resolution = 33;
    s.erosion.duration = .25; s.erosion.rainDuration = .15; s.erosion.talusPasses = 1;
    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        s.seed = seed; s.erosion.workers = 1;
        auto serial = TerrainGenerator::build(s);
        require(serial.drainage && serial.erosion->finalized, "drained preset skipped finalization/analysis");
        check(*serial.generationFields, *serial.drainage);
        require(serial.surface.heightmap().heights == serial.generationFields->crop().heights,
                "mesh/contact surface is not the exact final ground crop");
        require(serial.drainage->order.size() > serial.surface.heightmap().heights.size(), "drainage lost apron");
        same(*serial.drainage, TerrainDrainage::analyze(*serial.generationFields));
        s.erosion.workers = 4;
        auto parallel = TerrainGenerator::build(s);
        same(*serial.drainage, *parallel.drainage);
        require(serial.surface.heightmap().heights == parallel.surface.heightmap().heights &&
                serial.erosion->deposition == parallel.erosion->deposition, "parallel final terrain differs");
        s.preset = TerrainGenerator::Preset::ErodedValley;
        auto raw = TerrainGenerator::build(s);
        require(!raw.drainage && !raw.erosion->finalized, "raw erosion comparison preset changed");
        HydraulicErosion::settle(*raw.generationFields, *raw.erosion);
        require(raw.generationFields->heightmap.heights == serial.generationFields->heightmap.heights,
                "drained preset differs from explicit raw+settlement pipeline");
        s.preset = TerrainGenerator::Preset::DrainedValley;
    }
    require(!TerrainGenerator::build({}).drainage, "legacy enabled new drainage");
}
