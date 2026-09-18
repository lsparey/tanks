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

struct LegacyAttempt {
    uint32_t index = 0, seed = 0;
    TerrainPlayability::Status playability = TerrainPlayability::Status::NoTraversableRegion;
    bool hasSecondarySpawn = false; // Ready status alone isn't sufficient -- see selectLegacy's comment
    double generationMs = 0;
};
struct LegacyResult {
    uint32_t requestedSeed = 0;
    Status status = Status::Exhausted;
    std::vector<LegacyAttempt> attempts;
    std::optional<TerrainGenerator::BuildResult> accepted;
    // Deliberately NOT accepted->playability: that field can never be
    // populated for a legacy BuildResult (TerrainGenerator::build's Legacy
    // branch never runs combined water/playability -- settings.playability
    // itself requires settings.combinedWater, which legacy never sets).
    // Keeping this as a sibling field, instead of quietly leaving
    // accepted->playability empty, avoids a caller assuming it mirrors the
    // drained-valley convention.
    std::optional<TerrainPlayability::Result> playability;
    double elapsedMs = 0;
};

// Legacy counterpart of select(). The legacy preset has no combined-water
// pipeline to drive TerrainGenerator::build's own settings.playability
// path, so this runs TerrainPlayability::analyze() itself against each
// candidate's bare heightmap, using `waterThreshold`/`waterMaxDepth` as its
// water test (HeightmapFlood) -- pass the SAME values the caller will use
// to build the actually-rendered water field, or the accepted seed's
// search result and its rendered water can disagree. Requires
// requested.preset == Preset::Legacy. Accepts only a Ready status whose
// route also has a valid secondarySpawn -- Ready alone is not enough, since
// legacy terrain has no guarantee (unlike the valley presets' own shape)
// that a second flat/dry area exists anywhere on the map. Same
// retry/cancellation/observer contract as select(): synchronous, snapshot
// recipe/policy/observer at entry, fail loud on invalid input.
LegacyResult selectLegacy(const TerrainGenerator::Settings& requested, const TerrainPlayability::Settings& playability,
                          float waterThreshold, float waterMaxDepth, const Settings& settings = {},
                          const std::function<void(const LegacyAttempt&)>& observer = {}, std::stop_token stop = {});
} // namespace TerrainSelection
