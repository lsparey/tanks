#pragma once

#include <functional>
#include <stop_token>
#include "TerrainGenerator.h"

namespace TerrainSelection {
inline constexpr uint32_t kVersion = 1;
inline constexpr uint32_t kMaximumAttempts = 8;
struct Settings {
    uint32_t maximumAttempts = 1; // includes the requested seed; retries are explicit
};
enum class Status { Accepted, Exhausted, Cancelled };
const char* statusName(Status);
struct Attempt {
    uint32_t index = 0, seed = 0;
    TerrainPlayability::Status playability = TerrainPlayability::Status::NoTraversableRegion;
    uint32_t components = 0;
    double generationMs = 0, largestComponentArea = 0, selectedComponentArea = 0;
    double routeLength = 0, routeSpan = 0;
};
struct Result {
    uint32_t requestedSeed = 0;
    Status status = Status::Exhausted;
    std::vector<Attempt> attempts; // all completed candidates, including rejected work
    // Present only for Accepted; settings.seed records the actual chosen seed.
    // Acceptance covers the current STATIC playability criteria, before scenery.
    std::optional<TerrainGenerator::BuildResult> accepted;
    double elapsedMs = 0; // whole selection, including rejected builds and observer time
};

// Versioned, distinct candidate seeds. Attempt zero is exactly requestedSeed;
// unsigned arithmetic defines wraparound identically for every worker count.
uint32_t seedForAttempt(uint32_t requestedSeed, uint32_t attempt);

// Synchronous loading helper. Requires a drained-valley recipe with combined
// water and playability enabled. Only failed playability is retried; generator
// or observer exceptions propagate. No threshold changes or legacy fallback.
// Recipe, retry policy and observer are snapshotted at entry; callbacks cannot
// accidentally change the active selection by editing their caller's settings.
// Observer runs on the calling thread after each completed build and sees only
// a summary. Failed build resources are released before the next attempt.
// Cancellation is checked before a build and after the observer, including
// before accepting a successful candidate. It does NOT interrupt an erosion
// pass in progress, nor does the attempt bound impose a wall-clock deadline.
Result select(const TerrainGenerator::Settings&, const Settings& = {},
              const std::function<void(const Attempt&)>& observer = {}, std::stop_token stop = {});
} // namespace TerrainSelection
