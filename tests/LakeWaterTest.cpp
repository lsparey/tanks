#include "scene/TerrainGenerator.h"
#include "ShorelineReference.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
void require(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
void close(double a, double b, double tolerance, const char* message) {
    require(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance, message);
}
template<class F> void rejects(F&& fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid lake request accepted");
}
MacroTerrain::Fields fixture(int n) {
    MacroTerrain::Fields f;
    f.playableResolution = n; f.playableWorldSize = float(n - 1); f.spacing = 1;
    f.heightmap = {n, float(n - 1), std::vector<float>(size_t(n) * n, 2)};
    f.openFaces.resize(size_t(n) * n);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x)
            f.openFaces[size_t(z) * n + x] = (x == 0 ? 1 : 0) | (x == n - 1 ? 2 : 0) |
                (z == 0 ? 4 : 0) | (z == n - 1 ? 8 : 0);
    return f;
}
void check(const MacroTerrain::Fields& f, const TerrainDrainage::Result& d, const LakeWater::Result& w) {
    checkShorelineQueries(w.surface, f.playableWorldSize);
    close(w.generatedRunoff, d.generatedRunoff, 1e-7, "water policy changed total runoff supply");
    close(w.runoffResidual, 0, 1e-7, "lake policy loses runoff");
    close(w.exportedRunoff + w.basinLoss, w.generatedRunoff, 1e-7, "lake global budget does not close");
    double loss = 0, exported = 0;
    for (size_t b = 0; b < w.lakes.size(); ++b) {
        const auto& lake = w.lakes[b];
        require(lake.level == d.basins[b].spillElevation && lake.area > 0 && lake.volume > 0,
                "invalid lake geometry or lake exceeds spill level");
        close(lake.inflow, lake.loss + lake.outflow, 1e-8, "lake-local supply/loss budget drift");
        require(lake.loss >= 0 && lake.outflow >= 0, "negative lake flux");
        if (!lake.present) require(lake.outflow == 0, "under-supplied lake leaks downstream");
        loss += lake.loss;
    }
    for (size_t i = 0; i < w.discharge.size(); ++i) {
        require(std::isfinite(w.discharge[i]) && w.discharge[i] >= 0, "invalid resolved discharge");
        if (d.downstream[i] < 0) exported += w.discharge[i];
        if (d.basin[i] >= 0 && int32_t(i) != w.lakes[d.basin[i]].spillFrom)
            require(w.discharge[i] == 0, "non-canonical lake exit exports duplicate supply");
    }
    close(loss, w.basinLoss, 1e-8, "incorrect summed lake loss");
    close(exported, w.exportedRunoff, 1e-8, "incorrect boundary runoff");
    TerrainSurface ground(f.crop());
    const auto& mesh = w.surface.mesh();
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        require(glm::cross(b.position - a.position, c.position - a.position).y > 0,
                "degenerate or downward water triangle");
        glm::vec3 p = (a.position + b.position + c.position) / 3.0f;
        auto sample = w.surface.sampleAt(p.x, p.z);
        float depth = p.y - ground.heightAt(p.x, p.z);
        // Float interpolation can put the smallest clipped slivers on a bank;
        // otherwise every rendered triangle must answer with the same surface.
        if (depth > 2e-5f) {
            require(sample.has_value(), "rendered water is dry in CPU queries");
            close(sample->height, p.y, 2e-5, "water query disagrees with triangle plane");
            close(sample->depth, depth, 2e-5, "water query does not use final terrain triangle");
            require(sample->flow == glm::vec2(0), "lake has invented flow");
        }
        for (const auto* v : {&a, &b, &c})
            close(v->depth, v->position.y - ground.heightAt(v->position.x, v->position.z), 3e-5,
                  "water vertex depth disagrees with actual terrain");
    }
    require(!w.surface.sampleAt(f.playableWorldSize, 0), "outside query clamped into water");
}
void same(const LakeWater::Result& a, const LakeWater::Result& b) {
    require(a.discharge == b.discharge && a.surface.triangleLakes() == b.surface.triangleLakes() &&
            a.lakes.size() == b.lakes.size() && a.surface.mesh().indices == b.surface.mesh().indices &&
            a.surface.mesh().vertices.size() == b.surface.mesh().vertices.size() &&
            a.surface.shores().size() == b.surface.shores().size(), "nondeterministic lake result");
    for (size_t i = 0; i < a.lakes.size(); ++i) {
        const auto& x = a.lakes[i]; const auto& y = b.lakes[i];
        require(x.present == y.present && x.level == y.level && x.area == y.area && x.volume == y.volume &&
                x.inflow == y.inflow && x.loss == y.loss && x.outflow == y.outflow &&
                x.spillFrom == y.spillFrom && x.spillTo == y.spillTo, "nondeterministic lake budget");
    }
    for (size_t i = 0; i < a.surface.mesh().vertices.size(); ++i) {
        const auto& x = a.surface.mesh().vertices[i]; const auto& y = b.surface.mesh().vertices[i];
        require(x.position == y.position && x.depth == y.depth, "nondeterministic water mesh");
    }
    for (size_t i = 0; i < a.surface.shores().size(); ++i)
        require(a.surface.shores()[i].a == b.surface.shores()[i].a &&
                a.surface.shores()[i].b == b.surface.shores()[i].b, "nondeterministic shoreline");
}
}

int main() {
    // Analytic six-triangle pit: a hexagonal lake of area 3, volume 2. Dual
    // cells alone would incorrectly report only one unit of lake surface area.
    auto pit = fixture(3);
    pit.heightmap.heights[4] = 0;
    auto d = TerrainDrainage::analyze(pit);
    auto w = LakeWater::build(pit, d, {0, 0});
    check(pit, d, w);
    require(w.lakes.size() == 1 && w.lakes[0].present, "supplied pit has no lake");
    close(w.lakes[0].area, 3, 0, "wrong triangle-integrated lake area");
    close(w.lakes[0].volume, 2, 1e-12, "wrong triangle-integrated lake volume");
    close(w.surface.sampleAt(0, 0)->depth, 2, 0, "wrong pit depth");
    close(*w.surface.shorelineDistanceAt(0, 0), -std::sqrt(.5), 1e-6, "wrong interior shoreline distance");
    close(*w.surface.shorelineDistanceAt(.9f, -.9f), .8 / std::sqrt(2), 1e-6, "wrong exterior shoreline distance");
    require(!w.surface.sampleAt(.5f, -.5f), "zero-depth shore is underwater");
    require(!w.surface.sampleAt(.9f, -.9f), "dry terrain triangle inherits water from another quad triangle");
    close(*w.surface.shorelineDistanceAt(.5f, -.5f), 0, 1e-6, "shoreline distance not zero at bank");
    auto dryDrainage = TerrainDrainage::analyze(pit, {0, 0});
    auto noRain = LakeWater::build(pit, dryDrainage, {0, 0});
    require(!noRain.lakes[0].present && noRain.surface.mesh().indices.empty() &&
            !noRain.surface.sampleAt(0, 0) && !noRain.surface.shorelineDistanceAt(0, 0), "no supply created a lake");
    auto losses = LakeWater::build(pit, d, {1, 1});
    check(pit, d, losses);
    require(!losses.lakes[0].present && losses.surface.mesh().indices.empty() &&
            losses.lakes[0].outflow == 0, "under-supplied basin became a spill-level lake");

    // Multiple equal-height exits must not each emit the same accumulated
    // reservoir supply. All loss-free inflow leaves exactly once.
    auto multi = fixture(5);
    for (int z = 1; z < 4; ++z)
        for (int x = 1; x < 4; ++x) multi.heightmap.heights[size_t(z) * 5 + x] = 0;
    auto md = TerrainDrainage::analyze(multi);
    require(md.basins[0].outletLinks > 1, "multi-outlet fixture did not exercise multiple spill edges");
    auto mw = LakeWater::build(multi, md, {0, 0});
    check(multi, md, mw);
    close(mw.lakes[0].inflow, 9 * .023, 1e-12, "multi-outlet reservoir duplicated incoming runoff");

    // Three basins in series. Upstream losses must change lower-basin supply;
    // raw drainage's potential runoff cannot be reused after lake retention.
    auto series = fixture(9);
    std::fill(series.heightmap.heights.begin(), series.heightmap.heights.end(), 9);
    std::fill(series.openFaces.begin(), series.openFaces.end(), 0);
    series.openFaces[4 * 9] = MacroTerrain::NegativeX;
    const std::array<float, 9> channel{0, 1, 0, 3, 2, 2, 5, 4, 6};
    for (int x = 0; x < 9; ++x) series.heightmap.heights[4 * 9 + x] = channel[x];
    auto sd = TerrainDrainage::analyze(series);
    require(sd.basins.size() == 3, "series fixture lacks three basins");
    auto noLoss = LakeWater::build(series, sd, {0, 0});
    auto highLoss = LakeWater::build(series, sd, {1, 1});
    check(series, sd, noLoss); check(series, sd, highLoss);
    require(highLoss.lakes[0].inflow < noLoss.lakes[0].inflow, "upstream lake losses did not affect lower basin");

    // Partial shoreline triangles: every external mesh edge must lie on the
    // exact terrain/water intersection, and shared edges must be bit-identical.
    auto clipped = fixture(5);
    std::fill(clipped.heightmap.heights.begin(), clipped.heightmap.heights.end(), 4);
    clipped.heightmap.heights[12] = 0;
    clipped.heightmap.heights[10] = clipped.heightmap.heights[11] = 2;
    auto cd = TerrainDrainage::analyze(clipped);
    auto cw = LakeWater::build(clipped, cd, {0, 0});
    check(clipped, cd, cw);
    std::map<std::array<uint32_t, 4>, int> edges;
    const auto& mesh = cw.surface.mesh();
    auto key = [](glm::vec3 p) { return std::array<uint32_t, 2>{std::bit_cast<uint32_t>(p.x == 0 ? 0.f : p.x),
                                                           std::bit_cast<uint32_t>(p.z == 0 ? 0.f : p.z)}; };
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        for (size_t k = 0; k < 3; ++k) {
            auto a = key(mesh.vertices[mesh.indices[i + k]].position);
            auto b = key(mesh.vertices[mesh.indices[i + (k + 1) % 3]].position);
            if (b < a) std::swap(a, b);
            ++edges[{a[0], a[1], b[0], b[1]}];
        }
    }
    TerrainSurface clippedGround(clipped.crop());
    for (const auto& [edge, count] : edges) {
        require(count == 1 || count == 2, "non-manifold lake mesh edge");
        if (count == 1) {
            float x = (std::bit_cast<float>(edge[0]) + std::bit_cast<float>(edge[2])) * .5f;
            float z = (std::bit_cast<float>(edge[1]) + std::bit_cast<float>(edge[3])) * .5f;
            close(clippedGround.heightAt(x, z), 2, 1e-6, "cracked interior water edge");
        }
    }
    // Crop through an existing lake. The crop line must not become a bank or
    // change full-domain supply/loss. Only its mesh and query domain shrink.
    auto crop = multi;
    crop.playableResolution = 3; crop.playableWorldSize = 2; crop.apronCells = 1;
    auto cropped = LakeWater::build(crop, md, {0, 0});
    check(crop, md, cropped);
    close(cropped.lakes[0].area, mw.lakes[0].area, 0, "crop changed full lake surface area");
    require(*cropped.surface.shorelineDistanceAt(1, 0) < -.5f, "crop edge became an artificial bank");
    require(!cropped.surface.sampleAt(1.01f, 0), "water query leaked beyond cropped mesh");

    // Production spacing/apron with an off-grid depression exercises float
    // intersections and contact agreement independently of the erosion cost.
    auto full = fixture(293);
    full.spacing = 180.0f / 256;
    full.playableResolution = 257; full.playableWorldSize = 180; full.apronCells = 18;
    full.heightmap.worldSize = full.spacing * 292;
    for (int z = 0; z < 293; ++z) {
        for (int x = 0; x < 293; ++x) {
            float px = (x - 146) * full.spacing - .17f, pz = (z - 146) * full.spacing + .23f;
            full.heightmap.heights[size_t(z) * 293 + x] = .005f * pz -
                .5f * std::max(0.0f, 1 - (px * px + pz * pz) / 16);
        }
    }
    auto fullDrainage = TerrainDrainage::analyze(full);
    auto fullWater = LakeWater::build(full, fullDrainage);
    require(!fullWater.surface.mesh().indices.empty(), "full-spacing fixture has no clipped water");
    check(full, fullDrainage, fullWater);

    auto flat = fixture(3);
    std::fill(flat.heightmap.heights.begin(), flat.heightmap.heights.end(), -100);
    auto fd = TerrainDrainage::analyze(flat);
    auto fw = LakeWater::build(flat, fd);
    require(fw.lakes.empty() && fw.surface.mesh().indices.empty(), "absolute low ground created water");
    for (int bad = 0; bad < 10; ++bad) {
        auto f = pit; auto drainage = d; LakeWater::Settings s;
        if (bad == 0) f.heightmap.heights.pop_back();
        if (bad == 1) f.apronCells = 20;
        if (bad == 2) drainage.order.back() = drainage.order.front();
        if (bad == 3) drainage.downstream[4] = 4;
        if (bad == 4) drainage.basin[4] = 99;
        if (bad == 5) drainage.generatedRunoff = std::numeric_limits<double>::quiet_NaN();
        if (bad == 6) s.seepage = -1;
        if (bad == 7) f.heightmap.heights[0] = 1; // drainage for a different ground
        if (bad == 8) drainage.generatedRunoff = std::numeric_limits<double>::max();
        if (bad == 9) {
            std::fill(f.heightmap.heights.begin(), f.heightmap.heights.end(), std::numeric_limits<float>::max());
            f.heightmap.heights[4] = -std::numeric_limits<float>::max();
            drainage = TerrainDrainage::analyze(f);
        }
        rejects([&] { LakeWater::build(f, drainage, s); });
    }
    rejects([&] { w.surface.sampleAt(std::numeric_limits<float>::infinity(), 0); });
    rejects([&] { w.surface.shorelineDistanceAt(0, std::numeric_limits<float>::quiet_NaN()); });
    TerrainGenerator::Settings settings;
    settings.lakes.emplace();
    rejects([&] { TerrainGenerator::build(settings); });
    settings.preset = TerrainGenerator::Preset::DrainedValley;
    settings.resolution = 33;
    settings.erosion.duration = .25; settings.erosion.rainDuration = .15; settings.erosion.talusPasses = 1;
    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        settings.seed = seed; settings.erosion.workers = 1;
        auto build = TerrainGenerator::build(settings);
        require(build.water.has_value(), "lake-enabled generator skipped water stage");
        check(*build.generationFields, *build.drainage, *build.water);
        auto heights = build.generationFields->heightmap.heights;
        same(*build.water, LakeWater::build(*build.generationFields, *build.drainage));
        require(build.generationFields->heightmap.heights == heights, "water selection modified ground");
        settings.erosion.workers = 4;
        auto parallel = TerrainGenerator::build(settings);
        same(*build.water, *parallel.water);
    }
}
