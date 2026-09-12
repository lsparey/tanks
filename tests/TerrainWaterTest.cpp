#include "scene/TerrainGenerator.h"
#include "ShorelineReference.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void close(double a, double b, double tolerance, const char* message) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a - b) > tolerance)
        throw std::runtime_error(std::string(message) + ": " + std::to_string(a) + " vs " + std::to_string(b));
}
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid combined water request accepted");
}
MacroTerrain::Fields field(int n = 9) {
    MacroTerrain::Fields f;
    f.playableResolution = n; f.playableWorldSize = n - 1; f.spacing = 1;
    f.heightmap = {n, float(n - 1), std::vector<float>(size_t(n) * n)};
    f.openFaces.resize(size_t(n) * n); f.openFaces[size_t(n / 2) * n] = MacroTerrain::NegativeX;
    return f;
}
glm::vec2 position(const MacroTerrain::Fields& f, uint32_t i) {
    return {(float(int(i % f.heightmap.resolution) - f.apronCells) / (f.playableResolution - 1) - .5f) * f.playableWorldSize,
            (float(int(i / f.heightmap.resolution) - f.apronCells) / (f.playableResolution - 1) - .5f) * f.playableWorldSize};
}
void receiverReference(const MacroTerrain::Fields& f, const TerrainDrainage::Result& d, const LakeWater::Result& w,
                       const StreamNetwork::Result& n, const TerrainWater::Result& r) {
    std::vector<int32_t> nodeAt(f.heightmap.heights.size(), -1);
    for (uint32_t k = 0; k < n.nodes.size(); ++k) nodeAt[n.nodes[k].cell] = int32_t(k);
    for (uint32_t i = 0; i < r.streamLevels.size(); ++i) {
        float expected = f.heightmap.heights[i];
        // Independent path walk checks the linear downstream propagation.
        for (int32_t cell = int32_t(i); cell >= 0; cell = d.downstream[cell]) {
            int32_t basin = d.basin[cell];
            if (basin >= 0) {
                if (w.lakes[basin].present) expected = w.lakes[basin].level;
                break;
            }
            int32_t owner = nodeAt[cell];
            if (owner < 0) continue;
            const auto& a = n.nodes[owner];
            auto level = [](const auto& v) { return v.kind == StreamNetwork::Kind::DrySink ? v.ground : v.waterLevel; };
            expected = level(a);
            if (a.downstream >= 0) {
                const auto& b = n.nodes[a.downstream];
                auto start = glm::dvec2(a.position), edge = glm::dvec2(b.position) - start;
                double t = std::clamp(glm::dot(glm::dvec2(position(f, i)) - start, edge) / glm::dot(edge, edge), 0.0, 1.0);
                expected = float(std::lerp(double(level(a)), double(level(b)), t));
            }
            break;
        }
        close(r.streamLevels[i], expected, 0, "downstream receiver field disagrees with independent path walk");
    }
}
void check(const MacroTerrain::Fields& f, const TerrainWater::Result& r) {
    checkShorelineQueries(r.surface, f.playableWorldSize);
    const auto& mesh = r.surface.mesh();
    TerrainSurface ground(f.crop());
    require(mesh.indices.size() % 3 == 0 && r.streamTriangles + r.lakeTriangles == mesh.indices.size() / 3,
            "water triangle counts disagree");
    float half = f.playableWorldSize * .5f;
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto &a = mesh.vertices[mesh.indices[i]], &b = mesh.vertices[mesh.indices[i + 1]], &c = mesh.vertices[mesh.indices[i + 2]];
        auto normal = glm::cross(glm::dvec3(b.position) - glm::dvec3(a.position), glm::dvec3(c.position) - glm::dvec3(a.position));
        require(normal.y > 0, "degenerate or downward water triangle");
        for (const auto* v : {&a, &b, &c}) {
            require(std::abs(v->position.x) <= half && std::abs(v->position.z) <= half &&
                    std::isfinite(v->position.y) && std::isfinite(v->depth) && v->depth >= 0,
                    "water escaped crop or has invalid depth");
            close(glm::length(v->normal), 1, 1e-6, "non-unit water normal");
            require(glm::length(v->flow) <= 1.000001f, "invalid water flow");
            close(v->depth, v->position.y - ground.heightAt(v->position.x, v->position.z), 5e-5,
                  "water vertex depth disagrees with ground contact");
        }
        if (a.depth + b.depth + c.depth < 1e-5f) continue; // float-sized shoreline slivers
        for (glm::dvec3 weights : {glm::dvec3(.2, .3, .5), glm::dvec3(.6, .2, .2)}) {
            auto p = glm::dvec3(a.position) * weights.x + glm::dvec3(b.position) * weights.y + glm::dvec3(c.position) * weights.z;
            // Query coordinates are floats. Evaluate the mesh plane at those
            // SAME coordinates; rounding XZ alone can shift height visibly on
            // a steep triangle, or move out of a sub-ULP shoreline sliver.
            glm::dvec2 q(float(p.x), float(p.z));
            glm::dvec2 origin(a.position.x, a.position.z), ab(b.position.x - double(a.position.x), b.position.z - double(a.position.z));
            glm::dvec2 ac(c.position.x - double(a.position.x), c.position.z - double(a.position.z));
            auto cross = [](glm::dvec2 u, glm::dvec2 v) { return u.x * v.y - u.y * v.x; };
            double u = cross(q - origin, ac) / cross(ab, ac), v = cross(ab, q - origin) / cross(ab, ac);
            if (u <= 0 || v <= 0 || u + v >= 1) continue;
            double height = a.position.y + u * (double(b.position.y) - a.position.y) + v * (double(c.position.y) - a.position.y);
            auto sample = r.surface.sampleAt(float(q.x), float(q.y));
            require(sample.has_value(), "mesh interior is dry in shared water query");
            close(sample->height, height, 5e-5, "water query disagrees with rendered triangle plane");
            close(sample->depth, height - ground.heightAt(float(q.x), float(q.y)), 5e-5, "water query depth uses stale ground");
            close(glm::length(sample->flow - a.flow), 0, 1e-5, "mesh and query flow disagree");
            // On a sloping surface, flow follows decreasing water height.
            close(glm::length(a.flow - b.flow), 0, 0, "flow changes within one water triangle");
            require(double(a.flow.x) * a.normal.x + double(a.flow.y) * a.normal.z >= -1e-7,
                    "water flow climbs its surface");
        }
    }
    require(!r.surface.sampleAt(half + 1, 0) && !r.surface.sampleAt(std::numeric_limits<float>::max(), 0), "outside water query was clamped wet");
    rejects([&] { r.surface.sampleAt(std::numeric_limits<float>::quiet_NaN(), 0); });
    rejects([&] { r.surface.shorelineDistanceAt(0, std::numeric_limits<float>::infinity()); });
    if (!r.surface.shores().empty()) {
        auto far = r.surface.shorelineDistanceAt(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        require(far && std::isfinite(*far) && *far > 0, "far shoreline query overflowed");
    }
}
// Independent projected triangle coverage: interior samples must meet exactly
// one rendered surface, including where lake/stream polygons share a triangle.
void coverage(const TerrainWater::Result& r, float world) {
    const auto& mesh = r.surface.mesh();
    for (int z = 0; z < 37; ++z) for (int x = 0; x < 37; ++x) {
        glm::dvec2 p((x + .371) / 37 * world - world * .5, (z + .619) / 37 * world - world * .5);
        int hits = 0;
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            auto point = [&](size_t k) { auto v = mesh.vertices[mesh.indices[i + k]].position; return glm::dvec2(v.x, v.z); };
            auto a = point(0), b = point(1), c = point(2);
            auto cross = [](glm::dvec2 u, glm::dvec2 v) { return u.x * v.y - u.y * v.x; };
            double den = cross(b - a, c - a);
            double u = cross(p - a, c - a) / den, v = cross(b - a, p - a) / den;
            hits += u > 1e-8 && v > 1e-8 && u + v < 1 - 1e-8;
        }
        require(hits <= 1, "lake and stream surfaces overlap");
        require(bool(r.surface.sampleAt(float(p.x), float(p.y))) == (hits == 1), "water mesh has a query-visible hole");
    }
}
void seams(const TerrainWater::Result& r, float world) {
    struct Count { uint32_t count = 0; bool shore = false, crop = false; };
    std::map<std::array<float, 6>, Count> edges;
    const auto& mesh = r.surface.mesh();
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        for (size_t k = 0; k < 3; ++k) {
            const auto &a = mesh.vertices[mesh.indices[i + k]], &b = mesh.vertices[mesh.indices[i + (k + 1) % 3]];
            std::array<float, 3> p{a.position.x, a.position.y, a.position.z}, q{b.position.x, b.position.y, b.position.z};
            if (p > q) std::swap(p, q);
            auto& entry = edges[{p[0], p[1], p[2], q[0], q[1], q[2]}];
            ++entry.count;
            entry.shore = a.depth == 0 && b.depth == 0;
            entry.crop = (a.position.x == b.position.x && std::abs(a.position.x) == world * .5f) ||
                         (a.position.z == b.position.z && std::abs(a.position.z) == world * .5f);
        }
    }
    for (const auto& [key, count] : edges)
        require(count.count == 2 || (count.count == 1 && (count.shore || count.crop)),
                "unmatched interior water edge or overlapping triangles");
}
}

int main() {
    StreamNetwork::Settings settings; settings.minimumDischarge = .05;
    auto f = field();
    auto d = TerrainDrainage::analyze(f); auto w = LakeWater::build(f, d);
    auto n = StreamNetwork::build(f, d, w, settings);
    auto r = TerrainWater::build(f, d, w, n);
    require(n.confluences > 0 && r.streamTriangles > 0 && r.lakeTriangles == 0, "flat confluence fixture did not create stream water");
    check(f, r); receiverReference(f, d, w, n, r); coverage(r, f.playableWorldSize); seams(r, f.playableWorldSize);
    close(r.streamArea, 64, 1e-6, "flat sheet has gaps or overlaps at confluences");
    require(r.surface.shores().empty(), "full-domain boundary became an artificial bank");
    auto dryD = TerrainDrainage::analyze(f, {0, 0}); auto dryW = LakeWater::build(f, dryD);
    auto dryN = StreamNetwork::build(f, dryD, dryW, settings);
    auto dry = TerrainWater::build(f, dryD, dryW, dryN);
    require(dry.surface.mesh().indices.empty() && !dry.surface.sampleAt(0, 0), "zero rain created permanent water");

    auto pond = field();
    std::fill(pond.heightmap.heights.begin(), pond.heightmap.heights.end(), 5);
    for (int z = 3; z <= 5; ++z) for (int x = 3; x <= 5; ++x) pond.heightmap.heights[z * 9 + x] = 0;
    pond.heightmap.heights[4 * 9] = 0;
    pond.heightmap.heights[4 * 9 + 1] = pond.heightmap.heights[4 * 9 + 2] = 1;
    auto pd = TerrainDrainage::analyze(pond); auto pw = LakeWater::build(pond, pd);
    auto pn = StreamNetwork::build(pond, pd, pw, settings);
    auto pr = TerrainWater::build(pond, pd, pw, pn);
    check(pond, pr); receiverReference(pond, pd, pw, pn, pr); coverage(pr, pond.playableWorldSize); seams(pr, pond.playableWorldSize);
    require(pr.streamTriangles > 0 && pr.lakeTriangles > 0, "lake/stream fixture missed one surface type");
    auto centre = pr.surface.sampleAt(0, 0);
    require(centre && centre->kind == TerrainWater::Kind::Lake && centre->height == 1 && centre->flow == glm::vec2(0),
            "standing lake interior lost its fixed level or acquired flow");
    require(pr.surface.shorelineDistanceAt(0, 0).value() < 0, "wet shoreline distance is not signed");
    // The escaping sheet stands a bounded spill head above the crest, so the
    // lake connects to its outlet stream instead of pinching dry at the sill.
    auto spill = pr.surface.sampleAt(-3, 0);
    require(spill && spill->kind == TerrainWater::Kind::Stream && spill->depth > 0 &&
            spill->depth <= settings.spillHead + 1e-5f, "spill crossing lost its positive-depth sheet");
    auto span = pr.surface.sampleAt(-2.5f, 0);
    require(span && span->depth > 0, "spill span is dry between its nodes");
    StreamNetwork::Result noStreams;
    auto lakeOnly = TerrainWater::build(pond, pd, pw, noStreams);
    check(pond, lakeOnly); coverage(lakeOnly, pond.playableWorldSize);
    close(lakeOnly.lakeArea, pw.lakes[0].area, 1e-6, "lake-only union changed standing water area");
    require(lakeOnly.streamTriangles == 0, "empty stream graph created moving water");
    // A separate lake can be supplied by its own catchment without becoming
    // part of a distant stream's reconstructed wet component.
    auto isolated = field();
    std::fill(isolated.heightmap.heights.begin(), isolated.heightmap.heights.end(), 2);
    std::fill(isolated.openFaces.begin(), isolated.openFaces.end(), 0);
    isolated.openFaces[2 * 9] = MacroTerrain::NegativeX;
    for (int x = 0; x < 9; ++x) isolated.heightmap.heights[2 * 9 + x] = 0;
    isolated.heightmap.heights[6 * 9 + 6] = 0;
    auto id = TerrainDrainage::analyze(isolated); auto iw = LakeWater::build(isolated, id);
    StreamNetwork::Result single;
    for (uint32_t cell : {uint32_t(2 * 9 + 1), uint32_t(2 * 9)}) {
        StreamNetwork::Node node;
        node.cell = cell; node.position = position(isolated, cell); node.ground = 0; node.waterLevel = .5f;
        single.nodes.push_back(node);
    }
    single.nodes[0].downstream = 1; single.nodes[1].kind = StreamNetwork::Kind::Boundary;
    single.downstreamOrder = {1, 0};
    auto ir = TerrainWater::build(isolated, id, iw, single);
    auto isolatedLake = ir.surface.sampleAt(2, 2);
    require(!ir.connected[6 * 9 + 6] && isolatedLake && isolatedLake->kind == TerrainWater::Kind::Lake,
            "unconnected prediction acquired an invented stream supply");
    // Equidistant streams have different heads and separate drainage paths.
    // The high stream is first in node order: geometric nearest-edge ties used
    // to borrow its head and flood this bank of the LOWER stream over a ridge.
    auto divided = field();
    std::fill(divided.heightmap.heights.begin(), divided.heightmap.heights.end(), 5);
    std::fill(divided.openFaces.begin(), divided.openFaces.end(), 0);
    divided.openFaces[2 * 9] = divided.openFaces[6 * 9] = MacroTerrain::NegativeX;
    for (int x = 0; x < 9; ++x) {
        divided.heightmap.heights[2 * 9 + x] = 0;
        divided.heightmap.heights[3 * 9 + x] = .25f;
        divided.heightmap.heights[4 * 9 + x] = 1;
        divided.heightmap.heights[5 * 9 + x] = 2.1f;
        divided.heightmap.heights[6 * 9 + x] = 2;
    }
    auto dividedD = TerrainDrainage::analyze(divided); auto dividedW = LakeWater::build(divided, dividedD);
    StreamNetwork::Result pair;
    for (int z : {6, 2}) {
        uint32_t first = uint32_t(pair.nodes.size());
        for (int x = 4; x >= 0; --x) {
            StreamNetwork::Node node;
            node.cell = z * 9 + x; node.position = position(divided, node.cell);
            node.ground = divided.heightmap.heights[node.cell]; node.waterLevel = z == 6 ? 2.05f : .4f;
            node.downstream = x > 0 ? int32_t(pair.nodes.size() + 1) : -1;
            if (x == 0) node.kind = StreamNetwork::Kind::Boundary;
            pair.nodes.push_back(node);
        }
        for (int k = 4; k >= 0; --k) pair.downstreamOrder.push_back(first + k);
    }
    auto dividedWater = TerrainWater::build(divided, dividedD, dividedW, pair);
    receiverReference(divided, dividedD, dividedW, pair, dividedWater);
    require(dividedWater.streamLevels[4 * 9 + 4] == .4f && !dividedWater.surface.sampleAt(0, 0),
            "bank borrowed water head from a neighbouring catchment");
    auto sinkW = LakeWater::build(pond, pd, {1, 1});
    auto sinkN = StreamNetwork::build(pond, pd, sinkW, settings);
    auto sink = TerrainWater::build(pond, pd, sinkW, sinkN);
    check(pond, sink);
    require(!sink.surface.sampleAt(0, 0) && sink.lakeTriangles == 0, "unsupplied basin became a standing lake");
    for (const auto& node : sinkN.nodes)
        if (node.kind == StreamNetwork::Kind::DrySink)
            require(!sink.connected[node.cell], "dry sink has a positive-depth stream endpoint");
    // An under-supplied two-tier basin renders its partial lake at the
    // equilibrium level; the exposed upper tier stays dry ground.
    auto tiers = field();
    std::fill(tiers.heightmap.heights.begin(), tiers.heightmap.heights.end(), 5);
    for (int x = 0; x <= 3; ++x) tiers.heightmap.heights[4 * 9 + x] = x == 0 ? 0 : 1;
    tiers.heightmap.heights[4 * 9 + 6] = 2;
    for (int z = 3; z <= 5; ++z) {
        tiers.heightmap.heights[z * 9 + 4] = 0;
        tiers.heightmap.heights[z * 9 + 5] = .5f;
    }
    auto td = TerrainDrainage::analyze(tiers);
    auto fullTiers = LakeWater::build(tiers, td);
    LakeWater::Settings partialLoss;
    partialLoss.evaporation = fullTiers.lakes[0].inflow / 3.5;
    partialLoss.seepage = 0;
    auto tw = LakeWater::build(tiers, td, partialLoss);
    require(tw.lakes[0].partial && tw.lakes[0].level == .5f, "two-tier fixture missed its partial lake");
    auto tn = StreamNetwork::build(tiers, td, tw, settings);
    auto trw = TerrainWater::build(tiers, td, tw, tn);
    check(tiers, trw); receiverReference(tiers, td, tw, tn, trw);
    coverage(trw, tiers.playableWorldSize); seams(trw, tiers.playableWorldSize);
    auto deepTier = trw.surface.sampleAt(0, 0); // the deep cell at x4, row 4
    require(deepTier && deepTier->kind == TerrainWater::Kind::Lake && deepTier->height == .5f &&
            deepTier->depth == .5f, "partial lake interior lost its equilibrium level");
    require(!trw.surface.sampleAt(1, 0), "exposed partial-lake tier is wet"); // shallow cell at x5

    auto cropped = f; cropped.apronCells = 2; cropped.playableResolution = 5; cropped.playableWorldSize = 4;
    auto cr = TerrainWater::build(cropped, d, w, n);
    require(cr.connected == r.connected && cr.streamLevels == r.streamLevels && cr.surface.shores().empty(),
            "crop changed full-domain water or introduced a bank");
    check(cropped, cr);
    for (int bad = 0; bad < 7; ++bad) {
        auto ff = f; auto dd = d; auto nn = n;
        if (bad == 0) ff.heightmap.heights.pop_back();
        if (bad == 1) dd.basin[0] = 100;
        if (bad == 2) nn.nodes[0].waterLevel = std::numeric_limits<float>::quiet_NaN();
        if (bad == 3) nn.nodes[0].ground += 1;
        if (bad == 4) nn.nodes[0].downstream = int32_t(nn.nodes.size());
        if (bad == 5) nn.nodes[0].position.x += 1;
        if (bad == 6) nn.downstreamOrder[0] = nn.downstreamOrder[1];
        rejects([&] { TerrainWater::build(ff, dd, w, nn); });
    }
    TerrainGenerator::Settings s;
    s.combinedWater = true;
    rejects([&] { TerrainGenerator::build(s); });
    s.preset = TerrainGenerator::Preset::DrainedValley; s.lakes.emplace(); s.streams.emplace(); s.channelCarving.emplace();
    s.resolution = 33; s.erosion.duration = .25; s.erosion.rainDuration = .15; s.erosion.talusPasses = 1;
    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        s.seed = seed; s.erosion.workers = 1;
        auto a = TerrainGenerator::build(s);
        require(a.combinedWater && a.combinedWater->streamTriangles > 0, "generator omitted combined water mesh");
        check(*a.generationFields, *a.combinedWater);
        seams(*a.combinedWater, s.worldSize);
        s.erosion.workers = 4;
        auto b = TerrainGenerator::build(s);
        require(a.combinedWater->streamLevels == b.combinedWater->streamLevels && a.combinedWater->connected == b.combinedWater->connected,
                "nondeterministic combined water fields");
        const auto& x = a.combinedWater->surface.mesh(); const auto& y = b.combinedWater->surface.mesh();
        require(x.indices == y.indices && x.vertices.size() == y.vertices.size(), "nondeterministic combined water topology");
        for (size_t i = 0; i < x.vertices.size(); ++i)
            require(x.vertices[i].position == y.vertices[i].position && x.vertices[i].normal == y.vertices[i].normal &&
                    x.vertices[i].flow == y.vertices[i].flow && x.vertices[i].depth == y.vertices[i].depth,
                    "nondeterministic combined water geometry");
    }
    // Production crop coordinates exercise float grid inversion and clipped
    // seam/query agreement that a small integer-spaced fixture cannot cover.
    auto full = MacroTerrain::generate(257, 180, 7331, {});
    auto fullD = TerrainDrainage::analyze(full);
    auto fullW = LakeWater::build(full, fullD);
    auto fullN = StreamNetwork::build(full, fullD, fullW);
    auto fullWater = TerrainWater::build(full, fullD, fullW, fullN);
    check(full, fullWater); seams(fullWater, 180);
}
