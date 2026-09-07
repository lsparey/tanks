#include "scene/TerrainSelection.h"

#include <bit>
#include <charconv>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
template<class T> T number(std::string_view text) {
    T value{};
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size()) throw std::invalid_argument("invalid numeric argument");
    return value;
}
// Same physical mesh checksum as terrain_probe; selection never edits it.
uint64_t meshHash(const TerrainGenerator::MeshData& mesh) {
    uint64_t hash = 14695981039346656037ull;
    auto word = [&](uint32_t value) {
        for (int i = 0; i < 4; ++i) { hash ^= (value >> (8 * i)) & 255u; hash *= 1099511628211ull; }
    };
    for (const auto& v : mesh.vertices)
        for (float value : {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y})
            word(std::bit_cast<uint32_t>(value));
    for (uint32_t value : mesh.indices) word(value);
    return hash;
}
}

int main(int argc, char** argv) {
    try {
        TerrainGenerator::Settings recipe;
        recipe.preset = TerrainGenerator::Preset::DrainedValley; recipe.resolution = 257;
        recipe.erosion.workers = 4;
        recipe.lakes.emplace(); recipe.streams.emplace(); recipe.streamSections.emplace();
        recipe.channelCarving.emplace(); recipe.combinedWater = true; recipe.playability.emplace();
        TerrainSelection::Settings selection;
        uint32_t repeats = 1;
        for (int i = 1; i < argc; ++i) {
            std::string_view option(argv[i]);
            if (option == "--help") {
                std::cout << "terrain_selection_probe [--seed N] [--max-attempts 1..8] [--repeats N]\n"
                             "  [--min-area N] [--min-span N] [--resolution N] [--world-size N]\n"
                             "  [--erosion-seconds N] [--rain-seconds N] [--erosion-workers 1..4] [--talus-passes N]\n"
                             "Defaults: seed 7331; one attempt; 257-sample carved valley with combined water/playability.\n"
                             "CSV includes every completed attempt and total selection time. Exit 2 on exhaustion.\n";
                return 0;
            }
            if (++i >= argc) throw std::invalid_argument("missing option value");
            if (option == "--seed") recipe.seed = number<uint32_t>(argv[i]);
            else if (option == "--max-attempts") selection.maximumAttempts = number<uint32_t>(argv[i]);
            else if (option == "--repeats") repeats = number<uint32_t>(argv[i]);
            else if (option == "--min-area") recipe.playability->minimumConnectedArea = number<double>(argv[i]);
            else if (option == "--min-span") recipe.playability->minimumRouteSpan = number<double>(argv[i]);
            else if (option == "--resolution") recipe.resolution = number<int>(argv[i]);
            else if (option == "--world-size") recipe.worldSize = number<float>(argv[i]);
            else if (option == "--erosion-seconds") recipe.erosion.duration = number<double>(argv[i]);
            else if (option == "--rain-seconds") recipe.erosion.rainDuration = number<double>(argv[i]);
            else if (option == "--erosion-workers") recipe.erosion.workers = number<int>(argv[i]);
            else if (option == "--talus-passes") recipe.erosion.talusPasses = number<int>(argv[i]);
            else throw std::invalid_argument("unknown terrain selection option");
        }
        if (repeats < 1 || repeats > 100) throw std::invalid_argument("repeats must be 1..100");
        bool exhausted = false;
        std::vector<std::pair<uint32_t, TerrainPlayability::Status>> expected;
        std::optional<uint64_t> expectedHash;
        std::cout << std::setprecision(17)
                  << "selection_version,requested_seed,repeat,max_attempts,selection_status,selection_ms,resolution,world_size,erosion_seconds,rain_seconds,talus_passes,workers,min_area,min_span,attempt,seed,playability_status,generation_ms,components,largest_component_area,selected_component_area,route_length,route_span,accepted_mesh_fnv1a64\n";
        for (uint32_t repeat = 0; repeat < repeats; ++repeat) {
            auto result = TerrainSelection::select(recipe, selection, [&](const auto& attempt) {
                std::cerr << "Attempt " << attempt.index + 1 << '/' << selection.maximumAttempts << ", seed " << attempt.seed
                          << ": " << TerrainPlayability::statusName(attempt.playability) << '\n';
            });
            exhausted |= result.status != TerrainSelection::Status::Accepted;
            std::vector<std::pair<uint32_t, TerrainPlayability::Status>> actual;
            for (const auto& attempt : result.attempts) actual.emplace_back(attempt.seed, attempt.playability);
            std::optional<uint64_t> hash;
            if (result.accepted) hash = meshHash(result.accepted->mesh);
            if (repeat && (actual != expected || hash != expectedHash)) throw std::runtime_error("non-deterministic terrain selection");
            expected = std::move(actual); expectedHash = hash;
            for (const auto& attempt : result.attempts) {
                std::cout << TerrainSelection::kVersion << ',' << result.requestedSeed << ',' << repeat << ',' << selection.maximumAttempts << ','
                          << TerrainSelection::statusName(result.status) << ',' << result.elapsedMs << ',' << recipe.resolution << ',' << recipe.worldSize << ','
                          << recipe.erosion.duration << ',' << recipe.erosion.rainDuration << ',' << recipe.erosion.talusPasses << ',' << recipe.erosion.workers << ','
                          << recipe.playability->minimumConnectedArea << ',' << recipe.playability->minimumRouteSpan << ',' << attempt.index << ',' << attempt.seed << ','
                          << TerrainPlayability::statusName(attempt.playability) << ',' << attempt.generationMs << ',' << attempt.components << ','
                          << attempt.largestComponentArea << ',' << attempt.selectedComponentArea << ',' << attempt.routeLength << ',' << attempt.routeSpan << ',';
                if (hash && attempt.index + 1 == result.attempts.size()) std::cout << std::hex << *hash << std::dec;
                std::cout << '\n';
            }
            std::cerr << TerrainSelection::statusName(result.status) << " after " << result.attempts.size()
                      << " attempt(s), " << result.elapsedMs << " ms total; excludes GPU and scene startup.\n";
        }
        return exhausted ? 2 : 0;
    } catch (const std::exception& e) {
        std::cerr << "terrain_selection_probe: " << e.what() << '\n';
        return 1;
    }
}
