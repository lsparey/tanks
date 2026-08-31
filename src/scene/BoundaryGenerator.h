#pragma once

#include <memory>

#include "../render/CommandContext.h"
#include "../render/Mesh.h"
#include "../render/VulkanContext.h"

class Terrain;

// Builds the play-area boundary geometry: a square ring inset from the
// terrain's own edge (see Application's kBoundaryInsetFraction), used both
// for a ground decal tracing the ring and a vertical "wall of light" rising
// from it. Both meshes follow the same perimeter path -- see
// perimeterPoints in BoundaryGenerator.cpp -- so the ground line and the
// base of the wall always line up exactly.
class BoundaryGenerator {
public:
    // A thin ribbon flush with the terrain surface (conforms to its height
    // and normal, like TrackMark), tracing the perimeter square of
    // half-extent `halfExtent`. Single-sided (seen from above only), like
    // terrain/track marks.
    static std::unique_ptr<Mesh> buildLineMesh(VulkanContext& ctx, CommandContext& commands,
                                                const Terrain& terrain, float halfExtent);

    // A vertical ribbon rising `wallHeight` world units from the same
    // perimeter, straight up (world +Y) regardless of local terrain slope.
    // Built double-sided (both triangle windings), like Mesh::dome, since
    // it can plausibly be seen from either side of the boundary.
    static std::unique_ptr<Mesh> buildWallMesh(VulkanContext& ctx, CommandContext& commands,
                                                const Terrain& terrain, float halfExtent,
                                                float wallHeight);
};
