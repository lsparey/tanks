#include "scene/TerrainGenerator.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>

namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
uint32_t number(std::string_view text) {
    uint32_t value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size()) throw std::invalid_argument("invalid unsigned integer");
    return value;
}
template<class Surface> std::optional<float> linear(const Surface& surface, glm::vec2 p) {
    if (surface.shores().empty()) return std::nullopt;
    double best = std::numeric_limits<double>::infinity();
    for (const auto& s : surface.shores()) {
        glm::dvec2 a(s.a), edge = glm::dvec2(s.b) - a;
        double length = glm::dot(edge, edge);
        double t = length > 0 ? std::clamp(glm::dot(glm::dvec2(p) - a, edge) / length, 0.0, 1.0) : 0;
        auto delta = glm::dvec2(p) - (a + t * edge);
        best = std::min(best, glm::dot(delta, delta));
    }
    float distance = float(std::min(std::sqrt(best), double(std::numeric_limits<float>::max())));
    return surface.sampleAt(p.x, p.y) ? -distance : distance;
}
template<class Surface> void benchmark(const Surface& surface, const char* name, uint32_t seed,
                                       const std::vector<glm::vec2>& queries, uint32_t repeats) {
    std::vector<std::optional<float>> reference(queries.size()), indexed(queries.size());
    auto run = [&](bool useIndex) {
        auto start = Clock::now();
        for (size_t i = 0; i < queries.size(); ++i) {
            auto p = queries[i];
            if (useIndex) indexed[i] = surface.shorelineDistanceAt(p.x, p.y);
            else reference[i] = linear(surface, p);
        }
        return elapsed(start);
    };
    run(false); run(true); // warm both query paths; generation is measured separately
    for (uint32_t repeat = 0; repeat < repeats; ++repeat) {
        double linearMs, indexedMs;
        if (repeat % 2) { indexedMs = run(true); linearMs = run(false); }
        else { linearMs = run(false); indexedMs = run(true); }
        if (reference != indexed) throw std::runtime_error("indexed shoreline differs from exhaustive reference");
        double checksum = 0;
        for (size_t i = 0; i < reference.size(); ++i) if (reference[i]) {
            if (std::signbit(*reference[i]) != std::signbit(*indexed[i]))
                throw std::runtime_error("indexed shoreline sign differs from reference");
            checksum += *indexed[i];
        }
        auto geometry = surface.shores(); // exclude the copy: surface builders move their contours
        auto start = Clock::now();
        ShorelineIndex rebuilt(std::move(geometry));
        double buildMs = elapsed(start);
        if (rebuilt.indexBytes() != surface.shorelineIndexBytes()) throw std::runtime_error("index rebuild size changed");
        std::cout << seed << ',' << name << ',' << repeat << ',' << surface.shores().size() << ',' << queries.size() << ','
                  << surface.shorelineIndexBytes() << ',' << buildMs << ',' << linearMs << ',' << indexedMs << ','
                  << linearMs / indexedMs << ',' << checksum << '\n';
    }
}
}

int main(int argc, char** argv) {
    try {
        uint32_t seed = 7331, queryCount = 8192, repeats = 5;
        for (int i = 1; i < argc; ++i) {
            std::string_view option(argv[i]);
            if (option == "--help") {
                std::cout << "shoreline_probe [--seed N] [--queries N] [--repeats N]\n"
                             "Builds the full 257-sample carved valley, then compares signed BVH and linear queries.\n"
                             "CSV timings exclude generation; both query paths are warmed and checked exactly.\n";
                return 0;
            }
            if (i + 1 == argc) throw std::invalid_argument("missing option value");
            uint32_t value = number(argv[++i]);
            if (option == "--seed") seed = value;
            else if (option == "--queries") queryCount = value;
            else if (option == "--repeats") repeats = value;
            else throw std::invalid_argument("unknown shoreline probe option");
        }
        if (queryCount < 1 || queryCount > 1000000 || repeats < 1 || repeats > 100)
            throw std::invalid_argument("queries must be 1..1000000 and repeats 1..100");
        TerrainGenerator::Settings settings;
        settings.preset = TerrainGenerator::Preset::DrainedValley;
        settings.seed = seed; settings.resolution = 257; settings.erosion.workers = 4;
        settings.lakes.emplace(); settings.streams.emplace(); settings.streamSections.emplace();
        settings.channelCarving.emplace(); settings.combinedWater = true;
        auto terrain = TerrainGenerator::build(settings);
        std::mt19937 random(7331); // identical query distribution for every terrain seed
        auto coordinate = [&] { return float(double(random()) / std::mt19937::max() - .5) * settings.worldSize * 1.25f; };
        std::vector<glm::vec2> queries;
        for (uint32_t i = 0; i < queryCount; ++i) {
            float x = coordinate(), z = coordinate();
            queries.emplace_back(x, z);
        }
        std::cout << std::setprecision(17)
                  << "seed,surface,repeat,segments,queries,index_bytes,index_build_ms,linear_ms,indexed_ms,speedup,checksum\n";
        benchmark(terrain.water->surface, "lake", seed, queries, repeats);
        benchmark(terrain.combinedWater->surface, "combined", seed, queries, repeats);
    } catch (const std::exception& e) {
        std::cerr << "shoreline_probe: " << e.what() << '\n';
        return 1;
    }
}
