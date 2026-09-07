#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <glm/glm.hpp>

// Immutable full-domain contour geometry and exact nearest-segment queries.
// The BVH reorders segment IDs only, preserving contour/export order. No query
// allocations or mutable cache; copies and moves own all of their data.
class ShorelineIndex {
public:
    struct Segment { glm::vec2 a, b; };
    ShorelineIndex() = default;
    explicit ShorelineIndex(std::vector<Segment> segments);
    // Unsigned distance; the water surface supplies the wet/dry sign. Empty
    // contours return nullopt; non-finite queries throw even on an empty index.
    // Finite distances beyond float range saturate at float max.
    std::optional<float> distanceAt(float x, float z) const;
    const std::vector<Segment>& segments() const { return segments_; }
    size_t indexBytes() const;
    size_t payloadBytes() const;
private:
    struct Node {
        glm::dvec2 minimum{0}, maximum{0};
        uint32_t first = 0, count = 0, right = 0; // count == 0: left is this node + 1
    };
    uint32_t build(uint32_t first, uint32_t count);
    void nearest(uint32_t node, glm::dvec2 point, double& best) const;
    std::vector<Segment> segments_;
    std::vector<uint32_t> order_;
    std::vector<Node> nodes_;
};
