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
template<class F> void rejects(F&& fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid stream request accepted");
}
MacroTerrain::Fields fixture(int n) {
    MacroTerrain::Fields f;
    f.playableResolution = n; f.playableWorldSize = float(n - 1); f.spacing = 1;
    f.heightmap = {n, float(n - 1), std::vector<float>(size_t(n) * n, 0)};
    f.openFaces.resize(size_t(n) * n);
    f.openFaces[size_t(n / 2) * n] = MacroTerrain::NegativeX;
    return f;
}
void check(const MacroTerrain::Fields& f, const TerrainDrainage::Result& d, const LakeWater::Result& w,
           const StreamNetwork::Result& r, const StreamNetwork::Settings& s) {
    using Kind = StreamNetwork::Kind;
    std::vector<uint32_t> rank(r.nodes.size(), uint32_t(r.nodes.size())), incoming(r.nodes.size()), coverage(r.nodes.size());
    std::vector<double> inflow(r.nodes.size());
    require(r.downstreamOrder.size() == r.nodes.size(), "stream order dimensions disagree");
    for (uint32_t k = 0; k < r.downstreamOrder.size(); ++k) {
        uint32_t i = r.downstreamOrder[k];
        require(i < r.nodes.size() && rank[i] == r.nodes.size(), "stream order is not a permutation");
        rank[i] = k;
    }
    uint32_t confluences = 0, deficient = 0;
    for (uint32_t i = 0; i < r.nodes.size(); ++i) {
        const auto& node = r.nodes[i];
        require(node.cell < f.heightmap.heights.size(), "invalid stream grid vertex");
        require(std::isfinite(node.waterLevel) && node.waterLevel >= node.ground &&
                std::isfinite(node.width) && node.width >= s.widthAtThreshold && node.width <= s.maximumWidth &&
                node.requestedDepth >= s.depthAtThreshold && node.requestedDepth <= s.maximumDepth,
                "invalid stream width/depth/profile");
        close(node.availableDepth, double(node.waterLevel) - node.ground, 1e-6, "incorrect longitudinal clearance");
        bool lake = node.kind == Kind::LakeInlet || node.kind == Kind::LakeOutlet;
        if (lake) {
            require(node.lake >= 0 && w.lakes[node.lake].present && node.waterLevel == w.lakes[node.lake].level,
                    "stream does not meet lake at its fixed level");
        } else {
            close(node.depthDeficit, std::max(0.0f, node.requestedDepth - node.availableDepth), 1e-6, "unreported channel depth deficit");
        }
        if (node.kind == Kind::DrySink) require(!w.lakes[node.lake].present, "supplied lake marked as dry sink");
        if (node.downstream >= 0) {
            uint32_t j = uint32_t(node.downstream);
            require(j < r.nodes.size() && rank[j] < rank[i], "stream cycle or bad downstream order");
            require(d.downstream[node.cell] == int32_t(r.nodes[j].cell), "stream left the drainage mesh edges");
            require(node.waterLevel >= r.nodes[j].waterLevel, "stream profile flows uphill");
            require(node.discharge >= s.minimumDischarge, "sub-threshold stream was selected");
            close(node.discharge, w.discharge[node.cell], 0, "stream ignores lake loss/overflow resolution");
            close(glm::length(node.flow), 1, 1e-6, "invalid stream flow direction");
            require(glm::dot(node.flow, r.nodes[j].position - node.position) > 0, "stream flow faces upstream");
            ++incoming[j]; inflow[j] += node.discharge;
        } else {
            require(node.kind == Kind::Boundary || node.kind == Kind::LakeInlet || node.kind == Kind::DrySink,
                    "stream has an unexplained terminal");
        }
        confluences += node.incoming > 1;
        deficient += node.depthDeficit > 1e-5f;
    }
    require(confluences == r.confluences && deficient == r.deficientNodes, "stream summary counts disagree");
    for (uint32_t i = 0; i < r.nodes.size(); ++i) {
        const auto& node = r.nodes[i];
        require(node.incoming == incoming[i], "incorrect stream confluence degree");
        if (node.kind == Kind::LakeInlet || node.kind == Kind::DrySink)
            close(node.discharge, inflow[i], 1e-8, "reservoir inlet double-counts selected supply");
        else require(node.discharge + 1e-8 >= inflow[i], "confluence loses selected stream discharge");
    }
    for (const auto& reach : r.reaches) {
        require(reach.count >= 2 && size_t(reach.first) + reach.count <= r.reachNodes.size(), "invalid stream reach slice");
        double length = 0;
        for (uint32_t k = 0; k + 1 < reach.count; ++k) {
            uint32_t from = r.reachNodes[reach.first + k], to = r.reachNodes[reach.first + k + 1];
            require(from < r.nodes.size() && to < r.nodes.size() && r.nodes[from].downstream == int32_t(to),
                    "reach is not a connected sequence of stream nodes");
            ++coverage[from];
            length += glm::length(glm::dvec2(r.nodes[to].position) - glm::dvec2(r.nodes[from].position));
            if (k > 0) require(r.nodes[from].incoming == 1, "reach crosses an unsplit confluence");
        }
        close(reach.length, length, 1e-8, "incorrect world-space reach length");
        const auto& first = r.nodes[r.reachNodes[reach.first]];
        const auto& last = r.nodes[r.reachNodes[reach.first + reach.count - 1]];
        require(first.incoming != 1 && (last.incoming != 1 || last.downstream < 0), "reach is not maximal");
    }
    for (uint32_t i = 0; i < r.nodes.size(); ++i)
        require(coverage[i] == (r.nodes[i].downstream >= 0 ? 1u : 0u), "stream edge omitted or duplicated in reaches");
}
void same(const StreamNetwork::Result& a, const StreamNetwork::Result& b) {
    require(a.nodes.size() == b.nodes.size() && a.reachNodes == b.reachNodes && a.downstreamOrder == b.downstreamOrder &&
            a.reaches.size() == b.reaches.size(), "nondeterministic stream topology");
    for (size_t i = 0; i < a.nodes.size(); ++i) {
        const auto& x = a.nodes[i]; const auto& y = b.nodes[i];
        require(x.cell == y.cell && x.kind == y.kind && x.lake == y.lake && x.downstream == y.downstream &&
                x.incoming == y.incoming && x.discharge == y.discharge && x.position == y.position && x.flow == y.flow &&
                x.ground == y.ground && x.waterLevel == y.waterLevel && x.width == y.width &&
                x.requestedDepth == y.requestedDepth && x.availableDepth == y.availableDepth && x.depthDeficit == y.depthDeficit,
                "nondeterministic stream profile");
    }
    for (size_t i = 0; i < a.reaches.size(); ++i)
        require(a.reaches[i].first == b.reaches[i].first && a.reaches[i].count == b.reaches[i].count &&
                a.reaches[i].length == b.reaches[i].length, "nondeterministic stream reaches");
}
}

int main() {
    using Kind = StreamNetwork::Kind;
    StreamNetwork::Settings s;
    s.minimumDischarge = .05;
    auto flat = fixture(9);
    auto d = TerrainDrainage::analyze(flat);
    auto w = LakeWater::build(flat, d);
    auto r = StreamNetwork::build(flat, d, w, s);
    check(flat, d, w, r, s);
    require(r.confluences > 0 && r.reaches.size() > 1, "flat branching fixture has no confluences");
    // With no lake caps, the largest downstream requested depth backs up over
    // this flat, giving all tributaries exactly one common water elevation.
    for (const auto& node : r.nodes) close(node.waterLevel, r.nodes.front().waterLevel, 0, "flat confluence has discontinuous water levels");
    auto fewer = s; fewer.minimumDischarge = 2;
    require(StreamNetwork::build(flat, d, w, fewer).nodes.empty(), "sub-threshold catchment created streams");
    auto dd = TerrainDrainage::analyze(flat, {0, 0});
    auto dw = LakeWater::build(flat, dd);
    require(StreamNetwork::build(flat, dd, dw, s).nodes.empty(), "dry catchment created streams");

    // A lake with a three-vertex flat spill sill. Positive nominal stream depth
    // cannot be met at its fixed lake level; report the obstruction explicitly.
    auto pond = fixture(9);
    std::fill(pond.heightmap.heights.begin(), pond.heightmap.heights.end(), 5);
    for (int x = 0; x <= 6; ++x) pond.heightmap.heights[4 * 9 + x] = x == 0 ? 0 : x < 4 ? 1 : x < 6 ? 0 : 2;
    auto pd = TerrainDrainage::analyze(pond);
    auto pw = LakeWater::build(pond, pd);
    auto original = pond.heightmap.heights;
    s.minimumDischarge = .005;
    auto pr = StreamNetwork::build(pond, pd, pw, s);
    check(pond, pd, pw, pr, s);
    require(pr.deficientNodes >= 3 && pr.maximumDeficit > .08f, "flat spill sill was hidden by a floating water profile");
    require(pond.heightmap.heights == original, "profile generation silently excavated ground");
    bool sharedCell = false;
    for (const auto& inlet : pr.nodes) {
        if (inlet.kind != Kind::LakeInlet) continue;
        for (const auto& outlet : pr.nodes) {
            if (outlet.kind != Kind::LakeOutlet || outlet.cell != inlet.cell) continue;
            sharedCell = true;
            require(inlet.downstream == -1 && outlet.downstream >= 0 && inlet.waterLevel == outlet.waterLevel,
                    "lake inlet/outlet were joined into an artificial river through the basin");
        }
    }
    require(sharedCell, "fixture failed to exercise lake inlet/outlet at the same vertex");
    auto sink = LakeWater::build(pond, pd, {1, 1});
    auto sr = StreamNetwork::build(pond, pd, sink, s);
    check(pond, pd, sink, sr, s);
    require(std::none_of(sr.nodes.begin(), sr.nodes.end(), [](const auto& node) { return node.kind == Kind::LakeOutlet; }),
            "unsupplied lake created an outlet stream from raw drainage potential");
    require(std::any_of(sr.nodes.begin(), sr.nodes.end(), [](const auto& node) { return node.kind == Kind::DrySink; }),
            "under-supplied basin did not stop incoming streams");

    // Keep the full analysis apron. Changing only the crop cannot remove
    // upstream tributaries, sources or lake constraints from the network.
    auto crop = pond;
    crop.apronCells = 1; crop.playableResolution = 7; crop.playableWorldSize = 6;
    auto cr = StreamNetwork::build(crop, pd, pw, s);
    require(cr.reachNodes == pr.reachNodes && cr.nodes.size() == pr.nodes.size(), "crop removed stream topology");
    for (size_t i = 0; i < pr.nodes.size(); ++i) {
        const auto& a = pr.nodes[i]; const auto& b = cr.nodes[i];
        require(a.cell == b.cell && a.waterLevel == b.waterLevel && a.discharge == b.discharge &&
                a.width == b.width && a.depthDeficit == b.depthDeficit, "crop changed stream profiles");
        // Coordinate calculation anchors to each playable mesh; fractional
        // grid coordinates can round differently after changing that crop.
        close(glm::length(a.position - b.position), 0, 1e-6, "crop moved a stream guide");
    }
    for (int bad = 0; bad < 9; ++bad) {
        auto f = pond; auto drainage = pd; auto water = pw; auto settings = s;
        if (bad == 0) settings.minimumDischarge = 0;
        if (bad == 1) settings.maximumWidth = std::numeric_limits<float>::quiet_NaN();
        if (bad == 2) settings.maximumDepth = .001f;
        if (bad == 3) water.discharge.pop_back();
        if (bad == 4) water.discharge.back() = -1;
        if (bad == 5) drainage.order.back() = drainage.order.front();
        if (bad == 6) drainage.downstream[0] = 0;
        if (bad == 7) f.heightmap.heights.back() = std::numeric_limits<float>::infinity();
        if (bad == 8) water.lakes[0].outflow += 1;
        rejects([&] { StreamNetwork::build(f, drainage, water, settings); });
    }
    TerrainGenerator::Settings settings;
    settings.streams.emplace();
    rejects([&] { TerrainGenerator::build(settings); });
    settings.preset = TerrainGenerator::Preset::DrainedValley;
    settings.lakes.emplace(); settings.resolution = 33;
    settings.erosion.duration = .25; settings.erosion.rainDuration = .15; settings.erosion.talusPasses = 1;
    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        settings.seed = seed; settings.erosion.workers = 1;
        auto build = TerrainGenerator::build(settings);
        require(build.streams && !build.streams->nodes.empty(), "missing stream network");
        check(*build.generationFields, *build.drainage, *build.water, *build.streams, *settings.streams);
        same(*build.streams, StreamNetwork::build(*build.generationFields, *build.drainage, *build.water, *settings.streams));
        settings.erosion.workers = 4;
        auto parallel = TerrainGenerator::build(settings);
        same(*build.streams, *parallel.streams);
    }
}
