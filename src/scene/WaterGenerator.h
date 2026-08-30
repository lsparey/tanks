#pragma once

#include <memory>
#include <vector>

#include "../render/CommandContext.h"
#include "../render/Mesh.h"
#include "../render/VulkanContext.h"

class Terrain;

// Analyzes the terrain's heightmap for low-lying basins and builds a water
// surface mesh from them: connected low-lying basins (4-connected flood
// fill wherever height < threshold) each get their own flat water level,
// capped to at most maxDepth above that basin's own floor -- so separate
// bodies of water end up at different heights instead of one shared "sea
// level", and none of them get very deep. Only cells that end up actually
// submerged (height at or below their basin's capped water level -- not
// just "part of the low region", since a deep, uneven basin can have a
// shallow rim that a small maxDepth doesn't reach) get geometry, both to
// keep the shoreline exact to the grid and to avoid doubling the
// ray-traced shadow/AO cost of the whole map with a near-invisible water
// plane sitting flush over dry terrain.
class WaterGenerator {
public:
    // Per-cell result of the flood-fill analysis, kept around after mesh
    // generation so other systems (see isUnderwater) can query "is this
    // world position underwater" without re-running the flood fill.
    struct FloodField {
        int resolution = 0;
        float worldSize = 0.0f;
        float maxDepth = 1.0f;           // as passed to computeFloodField; used to normalize color depth
        std::vector<bool> submerged;     // true only for cells actually underwater
        std::vector<float> waterLevel;   // meaningful only where submerged[i] is true
    };

    static FloodField computeFloodField(const Terrain& terrain, float threshold, float maxDepth);

    // Returns nullptr if no cell ends up submerged (e.g. an unlucky terrain
    // seed with no basins deep/large enough to qualify).
    static std::unique_ptr<Mesh> buildMesh(VulkanContext& ctx, CommandContext& commands,
                                            const Terrain& terrain, const FloodField& field);

    // Nearest-cell lookup -- precise enough for placement checks (see
    // Application::spawnTrees) without needing bilinear interpolation.
    static bool isUnderwater(const FloodField& field, float worldX, float worldZ);
};
