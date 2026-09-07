#include "ShorelineReference.h"

#include <array>
#include <future>
#include <random>
#include <utility>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid shoreline input accepted");
}
void compare(const ShorelineIndex& index, glm::vec2 p) {
    auto expected = shorelineReference(index.segments(), p.x, p.y);
    auto actual = index.distanceAt(p.x, p.y);
    require(actual == expected, "BVH distance differs from exhaustive scan");
    if (actual) require(std::isfinite(*actual) && !std::signbit(*actual), "unsigned distance is not finite/non-negative");
}
}

int main() {
    ShorelineIndex empty;
    require(!empty.distanceAt(0, 0) && empty.payloadBytes() == 0, "empty index has a shore or retained payload");
    rejects([&] { empty.distanceAt(std::numeric_limits<float>::quiet_NaN(), 0); });
    rejects([&] { ShorelineIndex({{{0, 0}, {0, std::numeric_limits<float>::infinity()}}}); });
    ShorelineIndex line({{{-2, 1}, {2, 1}}});
    require(line.distanceAt(0, 3) == 2 && line.distanceAt(5, 5) == 5 && line.distanceAt(1, 1) == 0,
            "analytic segment distances are incorrect");
    ShorelineIndex point({{{2, 3}, {2, 3}}});
    require(point.distanceAt(5, 7) == 5, "zero-length shore is not a point");

    std::mt19937 rng(7331);
    auto coordinate = [&] { return (float(rng() % 2000001) - 1000000) / 1000; };
    std::vector<ShorelineIndex::Segment> segments;
    for (int i = 0; i < 4097; ++i) {
        glm::vec2 a(coordinate(), coordinate()), b = a + glm::vec2(coordinate(), coordinate()) * .01f;
        segments.push_back({a, i % 17 == 0 ? a : b});
    }
    // Many coincident centres, duplicates and long segments must still split
    // into balanced leaves and find interiors rather than nearest endpoints.
    for (int i = 1; i < 129; ++i) segments.push_back({{-float(i), -float(i)}, {float(i), float(i)}});
    for (int i = 0; i < 129; ++i) segments.push_back({{0, 0}, {0, 0}});
    ShorelineIndex index(segments);
    require(index.segments().size() == segments.size() && index.indexBytes() > 0, "missing index data");
    for (size_t i = 0; i < segments.size(); ++i) {
        require(index.segments()[i].a == segments[i].a && index.segments()[i].b == segments[i].b, "index reordered contour geometry");
        for (auto p : {segments[i].a, segments[i].b, (segments[i].a + segments[i].b) * .5f}) compare(index, p);
    }
    std::vector<glm::vec2> queries;
    for (int i = 0; i < 4096; ++i) queries.emplace_back(coordinate(), coordinate());
    for (auto p : queries) compare(index, p);
    float maximum = std::numeric_limits<float>::max();
    for (glm::vec2 p : {glm::vec2(maximum), glm::vec2(-maximum), glm::vec2(maximum, -maximum)}) {
        compare(index, p);
        require(index.distanceAt(p.x, p.y) == maximum, "far query did not saturate");
    }
    auto copy = index;
    auto moved = std::move(copy);
    index = ShorelineIndex();
    // Readers share an immutable index; owning copies survive the source's
    // destruction/replacement and concurrent queries cannot mutate traversal.
    std::array<std::future<void>, 4> readers;
    for (size_t worker = 0; worker < readers.size(); ++worker)
        readers[worker] = std::async(std::launch::async, [&, worker] {
            for (size_t i = worker; i < queries.size(); i += readers.size()) compare(moved, queries[i]);
        });
    for (auto& reader : readers) reader.get();

    // Adversarial float scales exercise conservative bounds and double
    // projection without overflow, including cancellation near an endpoint.
    std::vector<ShorelineIndex::Segment> extreme;
    for (int exponent : {-140, -60, -10, 0, 30, 80, 126}) {
        float scale = std::ldexp(1.f, exponent);
        for (int i = 0; i < 19; ++i)
            extreme.push_back({{scale, (float(i) / 32) * scale}, {-scale / 3, 0}});
    }
    ShorelineIndex extremes(extreme);
    for (const auto& s : extreme) {
        for (auto p : {s.a, s.b, (s.a + s.b) * .5f, glm::vec2(std::nextafter(s.b.x, maximum), s.b.y)}) compare(extremes, p);
    }
    compare(extremes, {maximum, -maximum});
    rejects([&] { moved.distanceAt(0, std::numeric_limits<float>::infinity()); });
}
