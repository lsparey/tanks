#pragma once

#include "LakeWater.h"

namespace StreamNetwork {

inline constexpr uint32_t kVersion = 1;
struct Settings {
    double minimumDischarge = 4; // same artistic volume/time units as LakeWater
    float widthAtThreshold = .8f, maximumWidth = 4;
    float depthAtThreshold = .08f, maximumDepth = .4f;
};
enum class Kind { Channel, Boundary, LakeInlet, LakeOutlet, DrySink };
const char* kindName(Kind kind);
struct Node {
    uint32_t cell = 0;
    Kind kind = Kind::Channel;
    int32_t lake = -1, downstream = -1; // node index; reservoirs split inlet/outlet nodes
    uint32_t incoming = 0;
    double discharge = 0;
    glm::vec2 position{0}, flow{0};
    float ground = 0, waterLevel = 0;
    float width = 0, requestedDepth = 0;
    // Longitudinal clearance diagnostics only. No excavation is applied, and
    // no bank/width feasibility is implied by this centreline calculation.
    float availableDepth = 0, depthDeficit = 0;
};
struct Reach {
    uint32_t first = 0, count = 0; // slice of Result::reachNodes, including both junctions
    double length = 0;
};
struct Result {
    std::vector<Node> nodes;
    std::vector<uint32_t> downstreamOrder; // downstream nodes precede upstream
    std::vector<Reach> reaches; // maximal paths between heads, junctions and terminals
    std::vector<uint32_t> reachNodes;
    uint32_t confluences = 0, deficientNodes = 0;
    float maximumDeficit = 0;
    double elapsedMs = 0;
    size_t payloadBytes() const;
};

// Read-only stream selection/profile prototype. Uses RESOLVED lake discharge,
// not the raw drainage potential. Reservoir interiors are not river routes;
// unsupplied basins stop streams and never generate an outlet stream.
// This result is a channel design guide, NOT a rendered/queryable water mesh.
Result build(const MacroTerrain::Fields& fields, const TerrainDrainage::Result& drainage,
             const LakeWater::Result& water, const Settings& settings = {});
void validate(const Settings& settings);

} // namespace StreamNetwork
