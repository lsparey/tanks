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
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid carving request accepted");
}
MacroTerrain::Fields field(float soilDepth = 1) {
    MacroTerrain::Fields f;
    int n = 9;
    f.playableResolution = n; f.playableWorldSize = n - 1; f.spacing = 1;
    f.heightmap = {n, float(n - 1), std::vector<float>(n * n)};
    f.soil.assign(n * n, soilDepth); f.bedrock.resize(n * n); f.erodibility.assign(n * n, .5f);
    f.openFaces.resize(n * n); f.openFaces[4 * n] = MacroTerrain::NegativeX;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            f.heightmap.heights[z * n + x] = x * .125f + std::abs(z - 4) * 2;
            f.bedrock[z * n + x] = f.heightmap.heights[z * n + x] - soilDepth;
        }
    }
    return f;
}
StreamNetwork::Settings streamSettings() {
    StreamNetwork::Settings s;
    s.minimumDischarge = .4; s.widthAtThreshold = s.maximumWidth = 2;
    s.depthAtThreshold = s.maximumDepth = .25;
    return s;
}
void check(const MacroTerrain::Fields& before, const MacroTerrain::Fields& after,
           const TerrainDrainage::Result& oldDrainage, const StreamNetwork::Result& oldStreams,
           const ChannelCarving::Result& r, const ChannelCarving::Settings& settings) {
    require(before.erodibility == after.erodibility && before.openFaces == after.openFaces, "carving changed unrelated fields");
    require(r.cutDepth.size() == before.soil.size() && r.protectedCells.size() == before.soil.size(), "missing carving fields");
    double soilOut = 0, rockOut = 0, volume = 0;
    uint32_t changed = 0;
    int n = before.heightmap.resolution;
    for (size_t i = 0; i < before.soil.size(); ++i) {
        double delta = double(before.heightmap.heights[i]) - after.heightmap.heights[i];
        require(delta >= 0 && delta <= settings.maximumCut, "carving added ground or exceeded the cut cap");
        close(r.cutDepth[i], delta, 1e-7, "cut diagnostic disagrees with physical terrain");
        require(after.heightmap.heights[i] == after.bedrock[i] + after.soil[i] && after.soil[i] >= 0,
                "carving broke the material column");
        require(after.bedrock[i] <= before.bedrock[i] && after.soil[i] <= before.soil[i], "carving created material");
        if (after.bedrock[i] < before.bedrock[i]) require(after.soil[i] == 0, "bedrock cut before soil was removed");
        if (r.protectedCells[i]) require(delta == 0 && before.soil[i] == after.soil[i] && before.bedrock[i] == after.bedrock[i],
                                         "lake footprint or rim was excavated");
        if (oldDrainage.basin[i] >= 0) require(r.protectedCells[i], "basin was not protected");
        int x = int(i % n), z = int(i / n);
        double area = double(before.spacing) * before.spacing * (x == 0 || x == n - 1 ? .5 : 1) * (z == 0 || z == n - 1 ? .5 : 1);
        soilOut += (double(before.soil[i]) - after.soil[i]) * area;
        rockOut += (double(before.bedrock[i]) - after.bedrock[i]) * area;
        volume += delta * area;
        changed += delta > 0;
    }
    require(changed == r.changedCells, "changed-cell count is wrong");
    close(soilOut, r.budget.exportedSoil, 1e-8, "soil export is unaccounted");
    close(rockOut, r.budget.exportedBedrock, 1e-8, "bedrock export is unaccounted");
    close(volume, r.budget.removedGround, 1e-8, "ground volume is unaccounted");
    close(r.budget.materialResidual, 0, std::max(1e-8, r.budget.initialSoil * 1e-12), "material budget does not close");
    close(volume - soilOut - rockOut, r.budget.surfaceRoundingDelta, 1e-8, "surface quantization is hidden");
    for (const auto& node : oldStreams.nodes) {
        if (node.downstream < 0 || oldDrainage.basin[node.cell] >= 0) continue;
        uint32_t j = oldStreams.nodes[node.downstream].cell;
        require(after.heightmap.heights[node.cell] >= after.heightmap.heights[j], "carved channel bed climbs downstream");
    }
    auto fresh = TerrainDrainage::analyze(after);
    for (size_t i = 0; i < before.soil.size(); ++i)
        if (oldDrainage.basin[i] >= 0)
            require(fresh.spillElevation[i] == oldDrainage.spillElevation[i], "carving changed an existing basin escape level");
}
}

int main() {
    auto settings = streamSettings();
    for (float soilDepth : {1.0f, .125f}) {
        auto f = field(soilDepth), before = f;
        auto d = TerrainDrainage::analyze(f);
        auto w = LakeWater::build(f, d);
        auto n = StreamNetwork::build(f, d, w, settings);
        auto r = ChannelCarving::apply(f, d, n);
        check(before, f, d, n, r, {});
        require(r.changedCells > 0, "fixture generated no physical channel");
        close(r.cutDepth[4 * 9 + 3], .25, 1e-7, "analytic channel centre has wrong cut depth");
        close(r.cutDepth[3 * 9 + 3], 0, 0, "channel cut beyond its width");
        if (soilDepth == 1) close(r.budget.exportedBedrock, 0, 0, "deep soil fixture cut bedrock");
        else require(r.budget.exportedBedrock > 0, "thin soil fixture did not reach bedrock");
        auto stale = f;
        rejects([&] { ChannelCarving::apply(stale, d, n); });
        require(stale.heightmap.heights == f.heightmap.heights && stale.soil == f.soil, "stale-input rejection mutated terrain");
    }
    auto f = field(), before = f;
    auto d = TerrainDrainage::analyze(f);
    auto w = LakeWater::build(f, d);
    auto n = StreamNetwork::build(f, d, w, settings);
    auto limited = ChannelCarving::apply(f, d, n, {.maximumCut = .1f});
    check(before, f, d, n, limited, {.maximumCut = .1f});
    require(limited.changedCells > 0, "bounded cut did no work");
    auto cropped = before;
    cropped.apronCells = 2; cropped.playableResolution = 5; cropped.playableWorldSize = 4;
    auto cropCut = ChannelCarving::apply(cropped, d, n, {.maximumCut = .1f});
    require(cropCut.cutDepth == limited.cutDepth && cropped.heightmap.heights == f.heightmap.heights,
            "changing only the crop changed full-domain channel edits");
    auto emptySettings = settings; emptySettings.minimumDischarge = 100;
    auto empty = StreamNetwork::build(before, d, w, emptySettings);
    auto untouched = before;
    require(ChannelCarving::apply(untouched, d, empty).changedCells == 0 && untouched.heightmap.heights == before.heightmap.heights,
            "empty stream selection modified terrain");

    // A lake and flat sill are deliberately preserved, including every vertex
    // of triangles that touch the basin. Nearby dry channels can still be cut.
    auto pond = field();
    std::fill(pond.heightmap.heights.begin(), pond.heightmap.heights.end(), 5);
    for (int x = 0; x <= 6; ++x) pond.heightmap.heights[4 * 9 + x] = x == 0 ? 0 : x < 4 ? 1 : x < 6 ? 0 : 2;
    for (size_t i = 0; i < pond.soil.size(); ++i) pond.bedrock[i] = pond.heightmap.heights[i] - pond.soil[i];
    auto pd = TerrainDrainage::analyze(pond); auto pw = LakeWater::build(pond, pd);
    auto ps = settings; ps.minimumDischarge = .005;
    auto pn = StreamNetwork::build(pond, pd, pw, ps);
    auto oldPond = pond;
    auto pr = ChannelCarving::apply(pond, pd, pn);
    check(oldPond, pond, pd, pn, pr, {});
    require(!pd.basins.empty() && pr.protectedCells[pd.basins[0].spillTo], "spill sill was not protected");
    for (int bad = 0; bad < 8; ++bad) {
        auto ff = before; auto dd = d; auto nn = n; ChannelCarving::Settings s;
        if (bad == 0) s.maximumCut = std::numeric_limits<float>::quiet_NaN();
        if (bad == 1) s.maximumCut = 3;
        if (bad == 2) ff.soil.pop_back();
        if (bad == 3) ff.bedrock[0] += 1;
        if (bad == 4) dd.order[0] = dd.order[1];
        if (bad == 5) nn.nodes[0].requestedDepth = -1;
        if (bad == 6) nn.nodes[0].downstream = int32_t(nn.nodes.size());
        if (bad == 7) nn.nodes[0].position.x += 1;
        auto copy = ff;
        rejects([&] { ChannelCarving::apply(ff, dd, nn, s); });
        require(ff.heightmap.heights == copy.heightmap.heights && ff.soil == copy.soil && ff.bedrock == copy.bedrock,
                "invalid carving input was partially committed");
    }
    TerrainGenerator::Settings s;
    s.channelCarving.emplace();
    rejects([&] { TerrainGenerator::build(s); });
    s.preset = TerrainGenerator::Preset::DrainedValley;
    s.lakes.emplace(); s.streams.emplace(); s.streamSections.emplace(); s.resolution = 33;
    s.erosion.duration = .25; s.erosion.rainDuration = .15; s.erosion.talusPasses = 1;
    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        s.seed = seed; s.erosion.workers = 1;
        auto uncut = s; uncut.channelCarving.reset();
        auto old = TerrainGenerator::build(uncut);
        auto a = TerrainGenerator::build(s);
        require(a.channelCarving && a.channelCarving->changedCells > 0, "generator omitted channel excavation");
        check(*old.generationFields, *a.generationFields, *old.drainage, *old.streams, *a.channelCarving, *s.channelCarving);
        close(a.channelCarving->budget.initialSoil, a.erosion->budget.finalSoil, 1e-8, "erosion/carving budget checkpoints do not meet");
        auto fresh = TerrainDrainage::analyze(*a.generationFields, {s.erosion.rainfall, s.erosion.infiltration});
        require(fresh.downstream == a.drainage->downstream && fresh.basin == a.drainage->basin &&
                fresh.runoff == a.drainage->runoff && fresh.spillElevation == a.drainage->spillElevation,
                "generator retained pre-cut drainage");
        auto freshWater = LakeWater::build(*a.generationFields, fresh, *s.lakes);
        require(freshWater.discharge == a.water->discharge && freshWater.surface.triangleLakes() == a.water->surface.triangleLakes(),
                "generator retained pre-cut lake water");
        for (const auto& node : a.streams->nodes) {
            require(node.ground == a.generationFields->heightmap.heights[node.cell], "stream profile retained pre-cut ground");
            if (node.downstream >= 0)
                require(a.drainage->downstream[node.cell] == int32_t(a.streams->nodes[node.downstream].cell),
                        "stream profile retained pre-cut routing");
        }
        auto hm = a.generationFields->crop();
        require(a.surface.heightmap().heights == hm.heights, "contact surface retained pre-cut ground");
        for (size_t i = 0; i < a.mesh.vertices.size(); ++i)
            require(a.mesh.vertices[i].position.y == hm.heights[i], "render mesh retained pre-cut ground");
        s.erosion.workers = 4;
        auto b = TerrainGenerator::build(s);
        require(a.channelCarving->cutDepth == b.channelCarving->cutDepth &&
                a.channelCarving->protectedCells == b.channelCarving->protectedCells &&
                a.generationFields->heightmap.heights == b.generationFields->heightmap.heights &&
                a.generationFields->soil == b.generationFields->soil && a.generationFields->bedrock == b.generationFields->bedrock &&
                a.drainage->downstream == b.drainage->downstream, "channel carving is not deterministic across workers");
    }
}
