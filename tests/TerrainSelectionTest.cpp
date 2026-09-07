#include "scene/TerrainSelection.h"

#include <cmath>
#include <set>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid selection request accepted");
}
TerrainGenerator::Settings recipe() {
    TerrainGenerator::Settings s;
    s.preset = TerrainGenerator::Preset::DrainedValley; s.resolution = 33;
    s.seed = 2654443100u;
    s.erosion.duration = .25; s.erosion.rainDuration = .15; s.erosion.talusPasses = 1;
    s.lakes.emplace(); s.streams.emplace(); s.channelCarving.emplace();
    s.combinedWater = true; s.playability.emplace();
    return s;
}
void same(const TerrainGenerator::BuildResult& a, const TerrainGenerator::BuildResult& b) {
    require(a.settings.seed == b.settings.seed && a.surface.heightmap().heights == b.surface.heightmap().heights &&
            a.mesh.indices == b.mesh.indices && a.mesh.vertices.size() == b.mesh.vertices.size(), "selection changed ground");
    for (size_t i = 0; i < a.mesh.vertices.size(); ++i)
        require(a.mesh.vertices[i].position == b.mesh.vertices[i].position && a.mesh.vertices[i].normal == b.mesh.vertices[i].normal,
                "selection changed the rendered surface");
    require(a.combinedWater->streamLevels == b.combinedWater->streamLevels &&
            a.combinedWater->surface.mesh().indices == b.combinedWater->surface.mesh().indices &&
            a.playability->flags == b.playability->flags && a.playability->route == b.playability->route &&
            a.playability->spawn->position == b.playability->spawn->position, "selection changed hydrology or navigation");
}
}

int main() {
    using namespace TerrainSelection;
    require(seedForAttempt(7331, 0) == 7331 && seedForAttempt(7331, 1) == 2654443100u &&
            seedForAttempt(7331, 2) == 1013911573u && seedForAttempt(4294967295u, 1) == 2654435768u,
            "versioned candidate sequence or unsigned wraparound changed");
    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        std::set<uint32_t> distinct;
        for (uint32_t attempt = 0; attempt < kMaximumAttempts; ++attempt) distinct.insert(seedForAttempt(seed, attempt));
        require(distinct.size() == kMaximumAttempts && seedForAttempt(seed, 0) == seed, "candidate seeds repeat or skip requested seed");
    }
    auto s = recipe();
    auto direct = TerrainGenerator::build(s);
    require(direct.playability->status == TerrainPlayability::Status::Ready, "first-success fixture has no spawn");
    uint32_t callbacks = 0;
    auto first = select(s, {3}, [&](const Attempt& attempt) {
        require(attempt.index == callbacks && attempt.seed == s.seed, "incorrect first-attempt notification");
        ++callbacks;
    });
    require(first.status == Status::Accepted && first.accepted && first.attempts.size() == 1 && callbacks == 1,
            "successful candidate did not stop retries");
    same(*first.accepted, direct);
    require(first.requestedSeed == s.seed && first.elapsedMs >= first.attempts[0].generationMs &&
            first.attempts[0].selectedComponentArea >= s.playability->minimumConnectedArea, "incorrect acceptance accounting");

    // Real coarse-grid fixture: seed 7331 has only 284.8 square units of
    // connected centre area; its next candidate has 949.2 and a viable spawn.
    for (int workers : {1, 4}) {
        auto retryRecipe = s; retryRecipe.seed = 7331; retryRecipe.erosion.workers = workers;
        auto retried = select(retryRecipe, {3});
        require(retried.status == Status::Accepted && retried.accepted && retried.attempts.size() == 2 &&
                retried.requestedSeed == 7331 && retried.accepted->settings.seed == 2654443100u &&
                retried.attempts[0].playability == TerrainPlayability::Status::InsufficientArea &&
                retried.attempts[1].playability == TerrainPlayability::Status::Ready,
                "failed first candidate did not select the next valid seed");
        require(retryRecipe.seed == 7331 && retryRecipe.playability->minimumConnectedArea == 400,
                "selection mutated the caller's recipe");
        same(*retried.accepted, direct);
    }

    auto impossible = s; impossible.playability->minimumConnectedArea = 1e12;
    auto single = select(impossible);
    require(single.status == Status::Exhausted && !single.accepted && single.attempts.size() == 1,
            "default selection silently retried or returned rejected terrain");
    for (int workers : {1, 4}) {
        impossible.erosion.workers = workers;
        callbacks = 0;
        auto exhausted = select(impossible, {3}, [&](const Attempt& attempt) {
            require(attempt.index == callbacks++ && attempt.seed == seedForAttempt(s.seed, attempt.index), "retry order is unstable");
        });
        require(exhausted.status == Status::Exhausted && !exhausted.accepted && exhausted.attempts.size() == 3 && callbacks == 3,
                "retry bound was exceeded or rejected terrain escaped");
        double total = 0;
        for (const auto& attempt : exhausted.attempts) {
            require(attempt.playability == TerrainPlayability::Status::InsufficientArea && attempt.selectedComponentArea == 0,
                    "retry silently relaxed playability criteria");
            total += attempt.generationMs;
        }
        require(exhausted.elapsedMs >= total, "total selection time hides rejected generation work");
    }
    // Settings are a request snapshot: UI/progress callbacks may edit controls
    // for the NEXT request without changing this request's seeds or thresholds.
    auto changing = impossible;
    Settings changingPolicy{3};
    auto snapshot = select(changing, changingPolicy, [&](const Attempt&) {
        changing.seed = 0; changing.playability->minimumConnectedArea = 1; changingPolicy.maximumAttempts = 8;
    });
    require(snapshot.status == Status::Exhausted && snapshot.attempts.size() == 3 && snapshot.requestedSeed == s.seed &&
            snapshot.attempts.back().seed == seedForAttempt(s.seed, 2), "callback changed an active request");

    std::stop_source stop;
    stop.request_stop(); callbacks = 0;
    auto cancelled = select(s, {3}, [&](const Attempt&) { ++callbacks; }, stop.get_token());
    require(cancelled.status == Status::Cancelled && !cancelled.accepted && cancelled.attempts.empty() && callbacks == 0,
            "pre-cancelled request generated a map");
    // Stop at both rejection and successful-completion boundaries. Even a
    // ready candidate must not be published after cancellation was requested.
    for (bool ready : {false, true}) {
        std::stop_source source;
        auto result = select(ready ? s : impossible, {3}, [&](const Attempt&) { source.request_stop(); }, source.get_token());
        require(result.status == Status::Cancelled && !result.accepted && result.attempts.size() == 1,
                "cancellation retried or published terrain");
        require((result.attempts[0].playability == TerrainPlayability::Status::Ready) == ready,
                "cancellation erased completed attempt outcome");
    }
    callbacks = 0;
    auto invalid = s; invalid.erosion.duration = -1;
    rejects([&] { select(invalid, {3}, [&](const Attempt&) { ++callbacks; }); });
    require(callbacks == 0, "generator validation error became a retried candidate");
    struct ObserverError {};
    bool propagated = false;
    try { select(impossible, {3}, [](const Attempt&) { throw ObserverError{}; }); }
    catch (const ObserverError&) { propagated = true; }
    require(propagated, "observer exception swallowed");
    rejects([&] { select(s, {0}); });
    rejects([&] { select(s, {9}); });
    rejects([&] { seedForAttempt(s.seed, 8); });
    invalid = s; invalid.playability.reset();
    rejects([&] { select(invalid); });
    invalid = s; invalid.combinedWater = false;
    rejects([&] { select(invalid); });
}
