#include "StreamNetwork.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace StreamNetwork {

const char* kindName(Kind kind) {
    switch (kind) {
        case Kind::Channel: return "channel";
        case Kind::Boundary: return "boundary";
        case Kind::LakeInlet: return "lake-inlet";
        case Kind::LakeOutlet: return "lake-outlet";
        case Kind::DrySink: return "dry-sink";
    }
    throw std::invalid_argument("invalid stream node kind");
}
void validate(const Settings& s) {
    if (!std::isfinite(s.minimumDischarge) || s.minimumDischarge < 1e-6 || s.minimumDischarge > 1e12 ||
        !std::isfinite(s.widthAtThreshold) || s.widthAtThreshold < .01f ||
        !std::isfinite(s.maximumWidth) || s.maximumWidth < s.widthAtThreshold || s.maximumWidth > 100 ||
        !std::isfinite(s.depthAtThreshold) || s.depthAtThreshold < .001f ||
        !std::isfinite(s.maximumDepth) || s.maximumDepth < s.depthAtThreshold || s.maximumDepth > 10 ||
        !std::isfinite(s.spillHead) || s.spillHead < 0 || s.spillHead > 1 ||
        !std::isfinite(s.headwaterRatio) || s.headwaterRatio < 1 || s.headwaterRatio > 100)
        throw std::invalid_argument("invalid stream selection/profile settings");
}
size_t Result::payloadBytes() const {
    return nodes.capacity() * sizeof(Node) + reaches.capacity() * sizeof(Reach) +
        (downstreamOrder.capacity() + reachNodes.capacity()) * sizeof(uint32_t);
}

Result build(const MacroTerrain::Fields& f, const TerrainDrainage::Result& d,
             const LakeWater::Result& w, const Settings& s) {
    auto started = std::chrono::steady_clock::now();
    validate(s);
    int n = f.heightmap.resolution, m = f.playableResolution, apron = f.apronCells;
    if (n < 2 || n > 4097 || m < 2 || m > n || apron < 0 || apron > n / 2 || m + 2 * apron != n ||
        !std::isfinite(f.spacing) || f.spacing <= 0 || !std::isfinite(f.playableWorldSize) || f.playableWorldSize <= 0 ||
        !std::isfinite(f.heightmap.worldSize) || f.heightmap.worldSize <= 0 ||
        std::abs(double(f.playableWorldSize) - double(f.spacing) * (m - 1)) > 1e-6 * f.playableWorldSize ||
        std::abs(double(f.heightmap.worldSize) - double(f.spacing) * (n - 1)) > 1e-6 * f.heightmap.worldSize)
        throw std::invalid_argument("invalid stream domain or crop");
    size_t count = size_t(n) * n;
    if (f.heightmap.heights.size() != count || f.openFaces.size() != count || d.order.size() != count ||
        d.downstream.size() != count || d.basin.size() != count || d.spillElevation.size() != count ||
        w.discharge.size() != count || w.lakes.size() != d.basins.size() ||
        !std::isfinite(w.generatedRunoff) || w.generatedRunoff < 0)
        throw std::invalid_argument("stream input dimensions or supply disagree");
    std::vector<uint32_t> rank(count, uint32_t(count));
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t i = d.order[k];
        if (i >= count || rank[i] != count) throw std::invalid_argument("invalid stream drainage order");
        rank[i] = k;
    }
    for (uint32_t i = 0; i < count; ++i) {
        int32_t j = d.downstream[i], b = d.basin[i];
        float h = f.heightmap.heights[i];
        if (!std::isfinite(h) || std::abs(h) > 1e6f || !std::isfinite(d.spillElevation[i]) ||
            d.spillElevation[i] < h || b < -1 || (b >= 0 && size_t(b) >= w.lakes.size()) ||
            (b >= 0) != (h < d.spillElevation[i]) || !std::isfinite(w.discharge[i]) || w.discharge[i] < 0 ||
            w.discharge[i] > w.generatedRunoff + 1e-8 * std::max(1.0, w.generatedRunoff) ||
            j < -1 || (j >= 0 && (size_t(j) >= count || rank[j] >= rank[i] || d.spillElevation[j] > d.spillElevation[i])) ||
            (j == -1) != (f.openFaces[i] != 0))
            throw std::invalid_argument("invalid or stale stream terrain/drainage/discharge");
        if (j >= 0) {
            int dx = int(i % n) - j % n, dz = int(i / n) - j / n;
            if (!((std::abs(dx) + std::abs(dz) == 1) || (dx == dz && std::abs(dx) == 1)))
                throw std::invalid_argument("stream route is not a terrain mesh edge");
        }
        if (b >= 0 && i != uint32_t(w.lakes[b].spillFrom) && w.discharge[i] != 0)
            throw std::invalid_argument("reservoir interior contains stream discharge");
    }
    for (size_t b = 0; b < w.lakes.size(); ++b) {
        const auto& lake = w.lakes[b];
        // Full lakes stand at their spill; partial lakes stand strictly below
        // it, consume their whole inflow and never generate an outlet stream.
        bool level = lake.partial ? lake.present && lake.level < d.basins[b].spillElevation && lake.outflow == 0
                                  : lake.level == d.basins[b].spillElevation;
        if (lake.spillFrom < 0 || size_t(lake.spillFrom) >= count || d.basin[lake.spillFrom] != int32_t(b) ||
            lake.spillTo != d.downstream[lake.spillFrom] || !level ||
            !std::isfinite(lake.outflow) || lake.outflow < 0 ||
            lake.outflow != w.discharge[lake.spillFrom])
            throw std::invalid_argument("stream lake outlet does not match resolved runoff");
    }

    Result r;
    if (!s.enabled) return r;
    // A lake inlet and outlet may occupy the same grid vertex but are separate
    // nodes. Connecting them would falsely depict the flood tree as a river
    // through a reservoir and confuse its inflow with its post-loss outflow.
    std::vector<int32_t> mainNode(count, -1), terminalNode(count, -1);
    auto nodeAt = [&](uint32_t cell, bool terminal) {
        auto& map = terminal ? terminalNode : mainNode;
        if (map[cell] >= 0) return uint32_t(map[cell]);
        Node node;
        node.cell = cell;
        node.lake = d.basin[cell];
        node.ground = f.heightmap.heights[cell];
        node.position = {(float(int(cell % n) - apron) / (m - 1) - .5f) * f.playableWorldSize,
                         (float(int(cell / n) - apron) / (m - 1) - .5f) * f.playableWorldSize};
        // A stream reaching a partial lake's basin above the waterline ends as
        // a dry sink; only an actually submerged entry meets the lake surface.
        if (node.lake >= 0) node.kind = terminal ?
            (w.lakes[node.lake].present && w.lakes[node.lake].level > node.ground ?
                Kind::LakeInlet : Kind::DrySink) : Kind::LakeOutlet;
        else node.kind = d.downstream[cell] < 0 ? Kind::Boundary : Kind::Channel;
        if (!terminal) node.discharge = w.discharge[cell];
        uint32_t id = uint32_t(r.nodes.size());
        map[cell] = int32_t(id);
        r.nodes.push_back(node);
        return id;
    };
    std::vector<uint8_t> sourced(count, !s.requireVisibleSource);
    if (s.requireVisibleSource) {
        // Walk upstream to downstream so an unsupported tributary never
        // becomes visible merely because it joins a supported river later.
        for (auto it = d.order.rbegin(); it != d.order.rend(); ++it) {
            uint32_t i = *it;
            int x = int(i % n), z = int(i / n), b = d.basin[i];
            bool exterior = x <= apron || z <= apron || x >= apron + m - 1 || z >= apron + m - 1;
            bool outlet = b >= 0 && w.lakes[b].present && i == uint32_t(w.lakes[b].spillFrom);
            sourced[i] |= exterior || outlet;
            if (sourced[i] && w.discharge[i] >= s.minimumDischarge && d.downstream[i] >= 0)
                sourced[d.downstream[i]] = 1;
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        int32_t b = d.basin[i], j = d.downstream[i];
        if (j < 0 || w.discharge[i] < s.minimumDischarge || !sourced[i]) continue;
        if (b >= 0 && (!w.lakes[b].present || i != uint32_t(w.lakes[b].spillFrom))) continue;
        uint32_t from = nodeAt(i, false), to = nodeAt(uint32_t(j), d.basin[j] >= 0);
        r.nodes[from].downstream = int32_t(to);
        ++r.nodes[to].incoming;
        if (d.basin[j] >= 0) r.nodes[to].discharge += w.discharge[i];
    }
    r.downstreamOrder.resize(r.nodes.size());
    std::iota(r.downstreamOrder.begin(), r.downstreamOrder.end(), 0u);
    std::sort(r.downstreamOrder.begin(), r.downstreamOrder.end(), [&](uint32_t a, uint32_t b) {
        auto ra = rank[r.nodes[a].cell], rb = rank[r.nodes[b].cell];
        return ra != rb ? ra < rb : a < b;
    });
    // Inlets meet the standing surface exactly. Outlets stand a bounded head
    // above the crest, as spilling water does, so the sheet leaving the lake
    // keeps positive depth where the saddle ground equals the lake level.
    auto lakeSurface = [&](const Node& node) {
        float level = w.lakes[node.lake].level;
        return node.kind == Kind::LakeOutlet ? level + std::min(s.spillHead, node.requestedDepth) : level;
    };
    std::vector<float> cap(r.nodes.size(), std::numeric_limits<float>::infinity());
    for (uint32_t i = 0; i < r.nodes.size(); ++i) {
        auto& node = r.nodes[i];
        double ratio = node.discharge / s.minimumDischarge;
        // Headwater taper: a just-selected reach is a 15%-size trickle that
        // grows to nominal size by headwaterRatio times the threshold, so
        // streams gather gradually instead of starting at full width.
        double t = s.headwaterRatio > 1 ? std::clamp((ratio - 1) / (double(s.headwaterRatio) - 1), 0.0, 1.0) : 1.0;
        double taper = .15 + .85 * (t * t * (3 - 2 * t));
        node.width = float(std::min(double(s.maximumWidth), s.widthAtThreshold * std::sqrt(ratio)) * taper);
        node.requestedDepth = float(std::min(double(s.maximumDepth), s.depthAtThreshold * std::cbrt(ratio)) * taper);
        if (node.kind == Kind::LakeInlet || node.kind == Kind::LakeOutlet) cap[i] = lakeSurface(node);
        if (node.downstream >= 0) {
            auto delta = glm::dvec2(r.nodes[node.downstream].position) - glm::dvec2(node.position);
            node.flow = glm::vec2(glm::normalize(delta));
        }
        r.confluences += node.incoming > 1;
    }
    // Carry fixed lake levels downstream as upper bounds. At a confluence all
    // incoming caps apply, so no branch has to climb into the common junction.
    for (auto it = r.downstreamOrder.rbegin(); it != r.downstreamOrder.rend(); ++it) {
        uint32_t i = *it;
        int32_t j = r.nodes[i].downstream;
        if (j >= 0) cap[j] = std::min(cap[j], cap[i]);
    }
    // Backwater pass: raise upstream profiles to a common downstream level
    // where needed, subject to lake caps. Confluences use ONE shared node, so
    // every incident reach has exactly the same junction position and level.
    for (uint32_t i : r.downstreamOrder) {
        auto& node = r.nodes[i];
        bool lake = node.kind == Kind::LakeInlet || node.kind == Kind::LakeOutlet;
        float desired = lake ? lakeSurface(node) : node.ground + node.requestedDepth;
        float downstream = node.downstream >= 0 ? r.nodes[node.downstream].waterLevel : desired;
        node.waterLevel = std::min(cap[i], std::max(desired, downstream));
        if (!std::isfinite(node.waterLevel) || node.waterLevel < node.ground ||
            (node.downstream >= 0 && node.waterLevel < downstream) || (lake && node.waterLevel != desired))
            throw std::invalid_argument("infeasible stream profile/lake constraints");
        node.availableDepth = node.waterLevel - node.ground;
        node.depthDeficit = lake ? 0 : std::max(0.0f, node.requestedDepth - node.availableDepth);
        if (node.depthDeficit > 1e-5f) ++r.deficientNodes;
        r.maximumDeficit = std::max(r.maximumDeficit, node.depthDeficit);
    }
    // Decompose the tree into maximal reaches, repeating shared junction IDs
    // at adjacent reach ends. No smoothing moves a route off its mesh edges.
    for (uint32_t i = 0; i < r.nodes.size(); ++i) {
        if (r.nodes[i].downstream < 0 || r.nodes[i].incoming == 1) continue;
        Reach reach;
        reach.first = uint32_t(r.reachNodes.size());
        r.reachNodes.push_back(i);
        uint32_t current = i;
        do {
            uint32_t next = uint32_t(r.nodes[current].downstream);
            reach.length += glm::length(glm::dvec2(r.nodes[next].position) - glm::dvec2(r.nodes[current].position));
            r.reachNodes.push_back(next);
            current = next;
        } while (r.nodes[current].downstream >= 0 && r.nodes[current].incoming == 1);
        reach.count = uint32_t(r.reachNodes.size()) - reach.first;
        r.reaches.push_back(reach);
    }
    r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return r;
}

} // namespace StreamNetwork
