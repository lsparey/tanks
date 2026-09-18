#include "TerrainSelection.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "HeightmapFlood.h"

namespace TerrainSelection {
const char* statusName(Status status) {
    switch (status) {
        case Status::Accepted: return "accepted";
        case Status::Exhausted: return "exhausted";
        case Status::Cancelled: return "cancelled";
    }
    throw std::invalid_argument("invalid terrain selection status");
}
uint32_t seedForAttempt(uint32_t requestedSeed, uint32_t attempt) {
    if (attempt >= kMaximumAttempts) throw std::invalid_argument("terrain attempt index exceeds the policy bound");
    // An odd step is a permutation of the uint32 seed space: no repeats before
    // wraparound of the entire space, and no library-dependent PRNG mapping.
    return requestedSeed + 0x9e3779b9u * attempt;
}
Result select(const TerrainGenerator::Settings& requested, const Settings& settings,
              const std::function<void(const Attempt&)>& observer, std::stop_token stop) {
    auto start = std::chrono::steady_clock::now();
    const auto initial = requested;
    const auto policy = settings;
    const auto onAttempt = observer;
    if (policy.maximumAttempts < 1 || policy.maximumAttempts > kMaximumAttempts)
        throw std::invalid_argument("terrain selection requires 1..8 attempts");
    if (initial.preset != TerrainGenerator::Preset::DrainedValley || !initial.lakes ||
        !initial.streams || !initial.combinedWater || !initial.playability)
        throw std::invalid_argument("terrain selection requires drained-valley, lakes, streams, combined water and playability");
    Result result;
    result.requestedSeed = initial.seed;
    result.attempts.reserve(policy.maximumAttempts);
    for (uint32_t index = 0; index < policy.maximumAttempts; ++index) {
        if (stop.stop_requested()) { result.status = Status::Cancelled; break; }
        auto recipe = initial;
        recipe.seed = seedForAttempt(initial.seed, index);
        auto candidate = TerrainGenerator::build(recipe);
        const auto& play = *candidate.playability;
        Attempt attempt;
        attempt.index = index; attempt.seed = recipe.seed; attempt.playability = play.status;
        attempt.components = uint32_t(play.components.size());
        attempt.generationMs = candidate.statistics.totalMs;
        for (const auto& component : play.components)
            attempt.largestComponentArea = std::max(attempt.largestComponentArea, component.area);
        attempt.routeLength = play.routeLength; attempt.routeSpan = play.routeSpan;
        if (play.status == TerrainPlayability::Status::Ready) {
            if (!play.spawn || play.route.size() < 2 || play.spawn->component >= play.components.size())
                throw std::logic_error("ready terrain lacks a valid spawn/route result");
            attempt.selectedComponentArea = play.components[play.spawn->component].area;
        }
        result.attempts.push_back(attempt);
        if (onAttempt) onAttempt(result.attempts.back());
        if (stop.stop_requested()) { result.status = Status::Cancelled; break; }
        if (play.status == TerrainPlayability::Status::Ready) {
            result.accepted = std::move(candidate);
            result.status = Status::Accepted;
            break;
        }
        // candidate dies here; the next expensive build never overlaps its
        // retained meshes, hydrology, erosion fields or navigation diagnostics.
    }
    result.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}

LegacyResult selectLegacy(const TerrainGenerator::Settings& requested, const TerrainPlayability::Settings& playability,
                          float waterThreshold, float waterMaxDepth, const Settings& settings,
                          const std::function<void(const LegacyAttempt&)>& observer, std::stop_token stop) {
    auto start = std::chrono::steady_clock::now();
    const auto initial = requested;
    const auto policy = settings;
    const auto playabilitySettings = playability;
    const auto onAttempt = observer;
    if (policy.maximumAttempts < 1 || policy.maximumAttempts > kMaximumAttempts)
        throw std::invalid_argument("terrain selection requires 1..8 attempts");
    if (initial.preset != TerrainGenerator::Preset::Legacy)
        throw std::invalid_argument("legacy terrain selection requires the legacy preset");
    TerrainPlayability::validate(playabilitySettings);
    LegacyResult result;
    result.requestedSeed = initial.seed;
    result.attempts.reserve(policy.maximumAttempts);
    for (uint32_t index = 0; index < policy.maximumAttempts; ++index) {
        if (stop.stop_requested()) { result.status = Status::Cancelled; break; }
        auto recipe = initial;
        recipe.seed = seedForAttempt(initial.seed, index);
        auto candidate = TerrainGenerator::build(recipe);
        auto field = HeightmapFlood::compute(candidate.surface.heightmap(), waterThreshold, waterMaxDepth);
        auto play = TerrainPlayability::analyze(candidate.surface,
            [&](uint32_t triangle) { return HeightmapFlood::triangleHasWater(field, candidate.surface, triangle); },
            playabilitySettings);
        LegacyAttempt attempt;
        attempt.index = index; attempt.seed = recipe.seed; attempt.playability = play.status;
        attempt.generationMs = candidate.statistics.totalMs;
        bool ready = play.status == TerrainPlayability::Status::Ready;
        bool accepted = ready && TerrainPlayability::secondarySpawn(play, candidate.surface).has_value();
        attempt.hasSecondarySpawn = accepted;
        result.attempts.push_back(attempt);
        if (onAttempt) onAttempt(result.attempts.back());
        if (stop.stop_requested()) { result.status = Status::Cancelled; break; }
        if (accepted) {
            result.playability = std::move(play);
            result.accepted = std::move(candidate);
            result.status = Status::Accepted;
            break;
        }
        // candidate dies here, same reasoning as select()'s equivalent comment.
    }
    result.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
} // namespace TerrainSelection
