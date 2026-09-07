#pragma once

#include "StreamNetwork.h"

namespace StreamSections {

inline constexpr uint32_t kVersion = 1;
struct Settings {
    float searchDistance = 16; // maximum bank search on EACH side, in world units
};
enum class End { Shore, DomainEdge, SearchLimit, DryCentre };
const char* endName(End end);
struct Point {
    double distance = 0, ground = 0; // outward distance from the centre
};
struct Bank {
    End end = End::DryCentre;
    uint32_t first = 0, count = 0; // slice of Result::points, centre to endpoint
    double distance = 0, area = 0, wettedPerimeter = 0;
};
struct Section {
    uint32_t from = 0, to = 0;
    double station = 0; // 0, .5 or 1 along this network edge
    glm::dvec2 centre{0}, leftDirection{0};
    double waterLevel = 0, ground = 0, requestedWidth = 0;
    Bank left, right;
    bool bounded() const { return left.end == End::Shore && right.end == End::Shore; }
};
struct Control {
    uint32_t from = 0, to = 0;
    bool dryFrom = false, dryTo = false;
    // Both dry means an entire zero-depth edge; otherwise a point pinch.
    // These carry routed supply but are NOT positive-depth water connections.
};
struct Result {
    std::vector<Section> sections; // three oriented surveys per selected edge
    std::vector<Point> points; // exact terrain triangle crossings and shoreline
    std::vector<Control> controls;
    uint32_t boundedSections = 0, drySections = 0;
    uint32_t domainLimitedSections = 0, searchLimitedSections = 0;
    double elapsedMs = 0;
    size_t payloadBytes() const;
};

// Read-only bank survey at the existing profile level. Finds the FIRST bank,
// integrating the piecewise-linear terrain, including actual mesh diagonals.
// Full-apron boundaries and search limits are reported, never treated as banks.
// Sections at junctions are oriented per incident edge; this does not join them
// into water polygons or certify the unsurveyed space between stations.
Result build(const MacroTerrain::Fields&, const StreamNetwork::Result&, const Settings& = {});
void validate(const Settings&);

} // namespace StreamSections
