#pragma once

#include <optional>
#include <utility>
#include "TerrainDrainage.h"
#include "TerrainSurface.h"
#include "ShorelineIndex.h"

namespace LakeWater {

inline constexpr uint32_t kVersion = 1;
struct Settings {
    // Artistic steady depth/time rates. Together these define full-basin loss
    // capacity. Under-supplied basins consume inflow and have no lake surface;
    // supplied basins stand at the spill level and pass excess downstream.
    double evaporation = .001;
    double seepage = .001;
};
struct Lake {
    bool present = false;
    float level = 0;
    double area = 0, volume = 0; // integrals at spill level, including absent candidates
    double inflow = 0, loss = 0, outflow = 0;
    int32_t spillFrom = -1, spillTo = -1;
};
struct Vertex {
    glm::vec3 position;
    float depth = 0;
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
using Shore = ShorelineIndex::Segment;
struct Sample {
    uint32_t lake = 0;
    float height = 0, depth = 0;
    glm::vec2 flow{0}; // standing lakes; no invented circulation field
};
struct Result;

class Surface {
public:
    // Finite outside queries are dry. Exact shoreline points have zero depth
    // and are dry; invalid coordinates throw. Levels and clipping share the
    // terrain's triangle split, including its diagonal tie rule.
    std::optional<Sample> sampleAt(float x, float z) const;
    // Exact distance to the full-apron contour segments, negative in water.
    // The playable crop edge is not a bank. Empty means no contour exists.
    // Exact nearest-segment BVH; no allocations or mutable query state.
    std::optional<float> shorelineDistanceAt(float x, float z) const;
    const Mesh& mesh() const { return mesh_; }
    const std::vector<Shore>& shores() const { return shoreline_.segments(); }
    size_t shorelineIndexBytes() const { return shoreline_.indexBytes(); }
    const std::vector<int32_t>& triangleLakes() const { return triangleLakes_; }
    size_t payloadBytes() const;
private:
    explicit Surface(HeightmapGenerator::Heightmap ground) : ground_(std::move(ground)) {}
    friend Result build(const MacroTerrain::Fields&, const TerrainDrainage::Result&, const Settings&);
    TerrainSurface ground_; // immutable playable ground, so queries cannot use a stale caller surface
    std::vector<int32_t> triangleLakes_;
    std::vector<float> levels_;
    Mesh mesh_;
    ShorelineIndex shoreline_;
};
struct Result {
    Surface surface;
    std::vector<Lake> lakes; // one entry per drainage basin, including absent lakes
    // Resolved full-domain flux on dry routing vertices. Basin interiors have
    // zero discharge except spillFrom, which stores the basin's outflow.
    std::vector<double> discharge;
    double generatedRunoff = 0, basinLoss = 0, exportedRunoff = 0, runoffResidual = 0;
    double elapsedMs = 0;
    size_t payloadBytes() const;
};

// Matching finalized ground/drainage required. Read-only; never alters height.
// Uniform local supply is recovered from drainage.generatedRunoff/domainArea.
// Basins route excess through their first (lowest-rank) spill edge. This makes
// the contracted basin graph acyclic even with several equal-height exits.
// This bounded full-or-dry policy does not model nested partially filled lakes.
Result build(const MacroTerrain::Fields& fields, const TerrainDrainage::Result& drainage,
             const Settings& settings = {});

} // namespace LakeWater
