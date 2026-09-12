#include "scene/TerrainRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double a, double b, double tolerance, const char* message) {
    require(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance, message);
}
template<class F> void rejects(F fn) {
    bool failed = false;
    try { fn(); } catch (const std::invalid_argument&) { failed = true; }
    require(failed, "invalid refinement accepted");
}
MacroTerrain::Fields fixture(bool curved) {
    MacroTerrain::Settings settings; settings.apronWidth = 1;
    auto f = MacroTerrain::generate(9, 8, 42, settings);
    int n = f.heightmap.resolution;
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
        size_t i = size_t(z) * n + x;
        f.soil[i] = .5f;
        float h = curved ? .01f * (x * x + z * z) : .125f * x - .25f * z;
        f.bedrock[i] = h - f.soil[i]; f.heightmap.heights[i] = f.bedrock[i] + f.soil[i];
    }
    return f;
}
}
int main() {
    for (bool curved : {false, true}) {
        auto original = fixture(curved), fine = original;
        auto r = TerrainRefinement::apply(fine);
        int n = original.heightmap.resolution, m = fine.heightmap.resolution;
        require(m == 2 * n - 1 && fine.playableResolution == 17 &&
                fine.apronCells == 2 * original.apronCells && fine.spacing * 2 == original.spacing &&
                fine.heightmap.worldSize == original.heightmap.worldSize &&
                fine.playableWorldSize == original.playableWorldSize, "refinement changed physical domain");
        require(r.sourceResolution == n && r.targetResolution == m, "missing source-grid identity");
        for (int z = 0; z < m; ++z) for (int x = 0; x < m; ++x) {
            size_t i = size_t(z) * m + x, coarse = size_t(z / 2) * n + x / 2;
            if (x % 2 == 0 && z % 2 == 0)
                require(fine.heightmap.heights[i] == original.heightmap.heights[coarse] &&
                        fine.bedrock[i] == original.bedrock[coarse] && fine.soil[i] == original.soil[coarse] &&
                        fine.openFaces[i] == original.openFaces[coarse], "coarse vertex changed");
            float low = original.heightmap.heights[coarse], high = low;
            for (int zz = 0; zz <= z % 2; ++zz) for (int xx = 0; xx <= x % 2; ++xx) {
                float h = original.heightmap.heights[coarse + zz * n + xx];
                low = std::min(low, h); high = std::max(high, h);
            }
            require(fine.heightmap.heights[i] >= low - 2e-6 && fine.heightmap.heights[i] <= high + 2e-6 &&
                    fine.soil[i] >= 0 && fine.heightmap.heights[i] == fine.bedrock[i] + fine.soil[i],
                    "refinement overshot the coarse cell or broke its column");
            int boundary = (x == 0 ? 1 : 0) | (x == m - 1 ? 2 : 0) | (z == 0 ? 4 : 0) | (z == m - 1 ? 8 : 0);
            require(fine.openFaces[i] == boundary, "refined open boundary is discontinuous");
            if (!curved) near(fine.heightmap.heights[i], .0625 * x - .125 * z, 1e-6, "plane changed at midpoint or boundary");
        }
        near(r.soilVolumeDelta, 0, 1e-7, "constant soil gained volume");
        if (!curved) near(r.bedrockVolumeDelta, 0, 1e-6, "planar bedrock gained volume");
        else {
            near(fine.heightmap.heights[7 * m + 7], .01 * (3.5 * 3.5 + 3.5 * 3.5), 1e-6, "cubic reconstruction lost curvature");
            float linear = (original.heightmap.heights[3 * n + 3] + original.heightmap.heights[4 * n + 4]) * .5f;
            require(std::abs(fine.heightmap.heights[7 * m + 7] - linear) > .001, "refinement only subdivides old triangles");
        }
        auto replay = original;
        TerrainRefinement::apply(replay);
        require(replay.heightmap.heights == fine.heightmap.heights, "refinement is not deterministic");
        auto twice = original;
        auto two = TerrainRefinement::apply(twice, 2);
        auto second = TerrainRefinement::apply(replay);
        require(twice.heightmap.heights == replay.heightmap.heights &&
                twice.bedrock == replay.bedrock && twice.soil == replay.soil &&
                twice.openFaces == replay.openFaces && twice.playableResolution == 33 &&
                twice.spacing * 4 == original.spacing && two.sourceResolution == n &&
                two.targetResolution == 4 * n - 3, "4x refinement differs from two bounded passes");
        near(two.soilVolumeDelta, r.soilVolumeDelta + second.soilVolumeDelta, 1e-9, "4x soil budget lost a pass");
        near(two.bedrockVolumeDelta, r.bedrockVolumeDelta + second.bedrockVolumeDelta, 1e-9, "4x rock budget lost a pass");
    }
    require(TerrainRefinement::parsePasses("off") == 0 && TerrainRefinement::parsePasses("on") == 1 &&
            TerrainRefinement::parsePasses("2x") == 1 && TerrainRefinement::parsePasses("4x") == 2,
            "refinement option maps to the wrong grid");
    rejects([] { TerrainRefinement::parsePasses("8x"); });
    for (int passes : {-1, 0, 3}) {
        auto f = fixture(false);
        rejects([&] { TerrainRefinement::apply(f, passes); });
    }
    for (int passes : {-1, 3})
        rejects([&] { TerrainRuntime::recipe(42, 3, 7, MacroTerrain::Landform::Mixed, 257, passes); });
    auto tooLarge = fixture(false); tooLarge.heightmap.resolution = 1026;
    rejects([&] { TerrainRefinement::apply(tooLarge, 2); });
    auto bad = fixture(false); bad.soil[3] = -1;
    auto before = bad.heightmap.heights;
    rejects([&] { TerrainRefinement::apply(bad); });
    require(bad.heightmap.heights == before, "failed refinement mutated input");
    bad = fixture(false); bad.heightmap.heights[3] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { TerrainRefinement::apply(bad); });
    rejects([] { TerrainRuntime::recipe(42, 3, 7, MacroTerrain::Landform::Mixed, 513, true); });
    TerrainGenerator::Settings invalid; invalid.refinementPasses = 1;
    rejects([&] { TerrainGenerator::build(invalid); });

    // Erosion is identical; downstream consumers must instead use the new grid.
    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        auto s = TerrainRuntime::recipe(seed, 2.222f, 4.48f);
        // Exercise the optional carving prototype even though runtime now
        // keeps only substantial lakes and skips stream incision.
        s.channelCarving.emplace(); s.streams->enabled = true;
        s.resolution = 33; s.erosion.duration = .25; s.erosion.rainDuration = .15;
        auto coarse = TerrainGenerator::build(s);
        for (int passes : {1, 2}) {
            s.refinementPasses = passes;
            auto fine = TerrainGenerator::build(s);
            require(fine.surface.heightmap().resolution == 1 + 32 * (1 << passes) && fine.refinement &&
                    fine.refinement->sourceResolution == coarse.generationFields->heightmap.resolution &&
                    fine.erosion->steps == coarse.erosion->steps && fine.erosion->water == coarse.erosion->water &&
                    fine.erosion->erosion == coarse.erosion->erosion && fine.erosion->deposition == coarse.erosion->deposition &&
                    fine.erosion->budget.finalSoil == coarse.erosion->budget.finalSoil &&
                    fine.erosion->peakWorkingBytes == coarse.erosion->peakWorkingBytes, "refinement altered the erosion simulation");
            require(fine.surface.heightmap().heights == fine.generationFields->crop().heights &&
                    fine.surface.heightmap().heights == fine.combinedWater->surface.ground().heightmap().heights,
                    "water/contact still use coarse ground");
            near(fine.channelCarving->budget.initialSoil,
                 fine.erosion->budget.finalSoil + fine.refinement->soilVolumeDelta +
                     (fine.outcrops ? fine.outcrops->soilVolumeDelta : 0),
                 1e-6,
                 "reconstruction volume is missing between erosion and carving budgets");
            for (size_t i = 0; i < fine.mesh.vertices.size(); ++i)
                require(fine.mesh.vertices[i].position.y == fine.surface.heightmap().heights[i], "render ground differs from contact");
            for (const auto& vertex : fine.combinedWater->surface.mesh().vertices) {
                require(vertex.depth <= *s.maximumWaterDepth + 5e-5f, "refinement exceeded maximum water depth");
                near(vertex.depth, vertex.position.y - fine.surface.heightAt(vertex.position.x, vertex.position.z),
                     5e-5, "water depth still uses coarse ground");
            }
            auto replay = TerrainGenerator::build(s);
            require(fine.surface.heightmap().heights == replay.surface.heightmap().heights &&
                    fine.combinedWater->surface.mesh().indices == replay.combinedWater->surface.mesh().indices,
                    "refined terrain/water replay changed");
        }
    }
}
