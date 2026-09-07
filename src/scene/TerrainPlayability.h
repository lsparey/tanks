#pragma once

#include <span>
#include "TerrainWater.h"
#include "CollisionSystem.h"

namespace TerrainPlayability {
inline constexpr uint32_t kVersion = 1;
struct Settings {
    // Current authored hull dimensions; runtime integration must supply the
    // loaded model's bounds if another model is selected. Handling is unchanged.
    float hullWidth = 2.222f, hullLength = 4.48f, clearance = .25f;
    float boundaryInsetFraction = .1f; // matches the current wall of light
    float maximumSlopeDegrees = 30, spawnSlopeDegrees = 12;
    double minimumConnectedArea = 400, minimumRouteSpan = 30;
};
enum Flag : uint8_t { Water = 1, Steep = 2, SpawnSteep = 4, Obstacle = 8, Boundary = 16 };
inline constexpr uint8_t kRouteBlocked = Water | Steep | Obstacle | Boundary;
enum class Status { Ready, NoTraversableRegion, InsufficientArea, NoDrySpawn, InsufficientRouteSpan };
const char* statusName(Status);
struct Component {
    uint32_t first = 0, cells = 0;
    double area = 0; // area available to tank centres, after footprint exclusion
};
struct Spawn {
    uint32_t cell = 0, component = 0;
    glm::vec3 position{0};
    glm::vec2 forward{0, 1};
};
struct Result {
    int resolution = 0; // one navigation cell per playable terrain quad
    double footprintRadius = 0, boundaryHalfExtent = 0;
    std::vector<uint8_t> terrainFlags, flags; // raw quads, then expanded whole-cell footprint
    std::vector<int32_t> component; // -1 for unsafe cells
    std::vector<Component> components;
    std::optional<Spawn> spawn;
    std::vector<uint32_t> route; // cell centres, spawn first; four-neighbour path
    double routeLength = 0, routeSpan = 0;
    Status status = Status::NoTraversableRegion;
    double elapsedMs = 0;
    size_t workingBytes = 0; // temporary vector capacities, excluding the result
    size_t payloadBytes() const;
};

// Read-only conservative analysis of the water surface's own FINAL ground.
// Every accepted whole navigation cell, expanded by the tank's rotational
// envelope, is dry, within the boundary, slope-safe and clear of supplied
// obstacle circles. Adjacent accepted cells give a continuous swept corridor.
// No terrain grading, hidden fallback, regeneration loop or scenery placement.
Result analyze(const TerrainWater::Surface&, const Settings& = {},
               std::span<const CollisionSystem::CircleObstacle> obstacles = {});
void validate(const Settings&);
} // namespace TerrainPlayability
