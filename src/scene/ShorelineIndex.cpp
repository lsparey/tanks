#include "ShorelineIndex.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace {
double boxDistanceSquared(glm::dvec2 p, glm::dvec2 minimum, glm::dvec2 maximum) {
    auto delta = glm::max(glm::max(minimum - p, p - maximum), glm::dvec2(0));
    return glm::dot(delta, delta);
}
}

ShorelineIndex::ShorelineIndex(std::vector<Segment> segments) : segments_(std::move(segments)) {
    if (segments_.size() > std::numeric_limits<uint32_t>::max() / 2)
        throw std::invalid_argument("too many shoreline segments");
    for (const auto& s : segments_)
        if (!std::isfinite(s.a.x) || !std::isfinite(s.a.y) || !std::isfinite(s.b.x) || !std::isfinite(s.b.y))
            throw std::invalid_argument("non-finite shoreline segment");
    order_.resize(segments_.size());
    std::iota(order_.begin(), order_.end(), 0u);
    if (!order_.empty()) build(0, uint32_t(order_.size()));
}

uint32_t ShorelineIndex::build(uint32_t first, uint32_t count) {
    Node node;
    node.minimum = glm::dvec2(std::numeric_limits<double>::infinity());
    node.maximum = -node.minimum;
    for (uint32_t k = first; k < first + count; ++k) {
        const auto& s = segments_[order_[k]];
        node.minimum = glm::min(node.minimum, glm::min(glm::dvec2(s.a), glm::dvec2(s.b)));
        node.maximum = glm::max(node.maximum, glm::max(glm::dvec2(s.a), glm::dvec2(s.b)));
    }
    int axis = node.maximum.x - node.minimum.x >= node.maximum.y - node.minimum.y ? 0 : 1;
    // Conservatively include rounding in a + t*(b-a), even for endpoints of
    // very different magnitudes. Bounds never trim the reference projection.
    auto padding = glm::max(glm::dvec2(1), glm::max(glm::abs(node.minimum), glm::abs(node.maximum))) *
                   (16 * std::numeric_limits<double>::epsilon());
    node.minimum -= padding;
    node.maximum += padding;
    uint32_t id = uint32_t(nodes_.size());
    nodes_.push_back(node);
    if (count <= 8) {
        nodes_[id].first = first;
        nodes_[id].count = count;
    } else {
        uint32_t half = count / 2;
        std::nth_element(order_.begin() + first, order_.begin() + first + half, order_.begin() + first + count,
                         [&](uint32_t a, uint32_t b) {
            double ca = double(segments_[a].a[axis]) + segments_[a].b[axis];
            double cb = double(segments_[b].a[axis]) + segments_[b].b[axis];
            return ca != cb ? ca < cb : a < b;
        });
        build(first, half);
        uint32_t right = build(first + half, count - half);
        nodes_[id].right = right;
    }
    return id;
}

void ShorelineIndex::nearest(uint32_t id, glm::dvec2 p, double& best) const {
    const auto& node = nodes_[id];
    if (node.count) {
        for (uint32_t k = node.first; k < node.first + node.count; ++k) {
            const auto& s = segments_[order_[k]];
            glm::dvec2 a(s.a), edge = glm::dvec2(s.b) - a;
            double length = glm::dot(edge, edge);
            double t = length > 0 ? std::clamp(glm::dot(p - a, edge) / length, 0.0, 1.0) : 0;
            auto delta = p - (a + t * edge);
            best = std::min(best, glm::dot(delta, delta));
        }
        return;
    }
    uint32_t left = id + 1, right = node.right;
    double a = boxDistanceSquared(p, nodes_[left].minimum, nodes_[left].maximum);
    double b = boxDistanceSquared(p, nodes_[right].minimum, nodes_[right].maximum);
    if (b < a) { std::swap(a, b); std::swap(left, right); }
    // Allow the final dot-product rounding when comparing lower bounds. This
    // only visits extra candidates; it never changes the segment calculation.
    constexpr double slack = 1 + 16 * std::numeric_limits<double>::epsilon();
    if (a <= best * slack) nearest(left, p, best);
    if (b <= best * slack) nearest(right, p, best);
}

std::optional<float> ShorelineIndex::distanceAt(float x, float z) const {
    if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("non-finite shoreline query");
    if (nodes_.empty()) return std::nullopt;
    double best = std::numeric_limits<double>::infinity();
    nearest(0, glm::dvec2(x, z), best);
    return float(std::min(std::sqrt(best), double(std::numeric_limits<float>::max())));
}
size_t ShorelineIndex::indexBytes() const {
    return order_.capacity() * sizeof(uint32_t) + nodes_.capacity() * sizeof(Node);
}
size_t ShorelineIndex::payloadBytes() const {
    return segments_.capacity() * sizeof(Segment) + indexBytes();
}
