#pragma once

#include "StreamNetwork.h"

namespace TerrainWater {
inline constexpr uint32_t kVersion = 3;
enum class Kind { Lake, Stream };
struct Vertex {
    glm::vec3 position{0}, normal{0, 1, 0};
    glm::vec2 flow{0};
    float depth = 0;
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
struct Sample {
    Kind kind = Kind::Lake;
    int32_t lake = -1;
    float height = 0, depth = 0;
    glm::vec2 flow{0};
};
struct Result;
class Surface {
public:
    // Exact terrain triangle convention; non-finite throws, finite outside is
    // dry, zero-depth points are dry. Lake wins equal-height stream/lake ties.
    std::optional<Sample> sampleAt(float x, float z) const;
    // Exact full-apron nearest contour, signed by sampleAt; immutable BVH.
    std::optional<float> shorelineDistanceAt(float x, float z) const;
    const TerrainSurface& ground() const { return ground_; }
    // Any positive-depth candidate on this actual ground triangle, including
    // arbitrarily small wet corners that a centre sample can miss.
    bool triangleHasWater(uint32_t triangle) const;
    const Mesh& mesh() const { return mesh_; }
    const std::vector<LakeWater::Shore>& shores() const { return shoreline_.segments(); }
    size_t shorelineIndexBytes() const { return shoreline_.indexBytes(); }
    size_t payloadBytes() const;
private:
    struct Triangle {
        int32_t lake = -1;
        float lakeLevel = 0;
        bool stream = false;
        glm::vec2 flow{0};
    };
    explicit Surface(HeightmapGenerator::Heightmap ground) : ground_(std::move(ground)) {}
    friend Result build(const MacroTerrain::Fields&, const TerrainDrainage::Result&,
                        const LakeWater::Result&, const StreamNetwork::Result&);
    TerrainSurface ground_;
    std::vector<float> streamLevels_;
    std::vector<Triangle> triangles_;
    Mesh mesh_;
    ShorelineIndex shoreline_; // full apron; crop edges are not shores
};
struct Result {
    Surface surface;
    std::vector<float> streamLevels; // full-domain reconstruction from downstream stream/lake receivers
    std::vector<uint8_t> connected; // positive-depth vertices connected to selected streams
    uint32_t streamTriangles = 0, lakeTriangles = 0; // playable emitted triangles
    double streamArea = 0, lakeArea = 0; // full-domain partitioned surface areas in XZ
    double elapsedMs = 0;
    size_t payloadBytes() const;
};

// Read-only static surface reconstruction. Banks inherit levels from their
// first downstream stream/lake, projecting onto the receiver's outgoing edge.
// Levels interpolate on terrain triangles; graph vertices retain shared levels.
// Reservoir vertices retain lake levels; dry sinks end at ground. Only wet
// components touching selected streams survive. Each triangle is partitioned
// into stream/lake upper-envelope polygons, then clipped at actual ground.
// This is a geometric prototype, not an additional fluid/loss simulation.
Result build(const MacroTerrain::Fields&, const TerrainDrainage::Result&,
             const LakeWater::Result&, const StreamNetwork::Result&);
} // namespace TerrainWater
