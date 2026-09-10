#pragma once

#include "TerrainGenerator.h"
#include "TreeInstance.h"

// CPU-owned runtime data, independently testable without a window or uploads.
namespace TerrainRuntime {
TerrainGenerator::Settings recipe(uint32_t seed, float hullWidth, float hullLength,
                                  MacroTerrain::Landform = MacroTerrain::Landform::Mixed, int resolution = 257, int refinementPasses = 0);

class Reservation {
public:
    // Protect the selected route plus a connected minimum-area neighbourhood
    // of its spawn. Reserve all raw quads touched by their expanded envelopes,
    // matching playability's closed-cell hazard rules exactly.
    Reservation(const TerrainSurface&, const TerrainPlayability::Result&, double minimumArea);
    bool allows(const CollisionSystem::CircleObstacle&) const;
    double protectedArea() const { return protectedArea_; }
private:
    std::vector<double> coordinates_;
    std::vector<uint8_t> reserved_;
    double protectedArea_ = 0;
};

struct State {
    TerrainSurface ground;
    std::optional<TerrainWater::Surface> water;
    std::optional<TerrainPlayability::Result> navigation;
    std::optional<Reservation> reservation;
    std::optional<TerrainPlayability::Settings> playabilitySettings;

    bool allowsScenery(glm::vec2 center, float radius) const;

    // Re-analyze actual final collision proxies, but keep the original spawn
    // and route. Returns their remaining connected area; throws on regression.
    double verifyObstacles(std::span<const CollisionSystem::CircleObstacle>) const;
};

// Moves only the ground, combined water and navigation into runtime ownership.
// Mesh arrays remain available for upload; generation diagnostics stay in build
// and are freed when it leaves loading. Unaccepted valley builds are rejected.
State retain(TerrainGenerator::BuildResult& build);

// Places 100 trees as grove clusters with scattered loners, preferring flat,
// moist ground and rejecting steep slopes; spacing stays >= 3 m and scales in
// [.8, 1.4]. The radius bounds all bark variants at unit scale. Failure is
// explicit; no rejected last candidate is forced into water or the route.
std::vector<TreeInstance> placeTrees(const State&, uint32_t seed, int variants, float barkRadius);
} // namespace TerrainRuntime
