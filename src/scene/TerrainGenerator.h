#pragma once

#include <cstddef>
#include <optional>
#include "MacroTerrain.h"
#include "TerrainRefinement.h"
#include "HydraulicErosion.h"
#include "TerrainDrainage.h"
#include "LakeWater.h"
#include "StreamNetwork.h"
#include "StreamSections.h"
#include "ChannelCarving.h"
#include "TerrainWater.h"
#include "TerrainSurface.h"
#include "TerrainPlayability.h"

namespace TerrainGenerator {

// Recipe version is scoped by preset. Legacy v1 stays available for comparison.
inline constexpr uint32_t kVersion = 1;
enum class Preset { Legacy, RollingValley, ErodedValley, DrainedValley };
const char* presetName(Preset preset);
inline constexpr std::array<uint32_t, 16> kRegressionSeeds{
    7331, 0, 1, 2, 7, 42, 123, 256, 1024, 4096, 12345, 65535,
    99991, 1000003, 2147483648u, 4294967295u};

struct Settings {
    Preset preset = Preset::Legacy;
    uint32_t version = kVersion;
    uint32_t seed = 7331;
    int resolution = 256; // macro/erosion grid
    int refinementPasses = 0; // 0: off; 1: 2x intervals; 2: 4x intervals after settlement
    float worldSize = 180.0f;
    float amplitude = 2.2f; // legacy recipe only
    MacroTerrain::Settings macro; // valley recipes
    HydraulicErosion::Settings erosion; // eroded/drained-valley prototypes
    std::optional<LakeWater::Settings> lakes; // opt-in drained-valley extension
    std::optional<StreamNetwork::Settings> streams; // requires lake loss/overflow resolution
    std::optional<StreamSections::Settings> streamSections; // exact bank/spill survey; requires streams
    std::optional<ChannelCarving::Settings> channelCarving; // opt-in terrain edits, followed by fresh hydrology
    bool combinedWater = false; // terrain-clipped stream/lake surface; requires stream profiles
    std::optional<TerrainPlayability::Settings> playability; // read-only final-ground spawn/route analysis
};

// Upload-independent vertex data: positions in world units, unit shading
// normals, and UVs repeating every three world units. No Vulkan types/handles.
struct MeshVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
};
struct Statistics {
    double heightfieldMs = 0;
    double surfaceMs = 0;
    double meshMs = 0;
    double totalMs = 0;
    double erosionMs = 0;
    double settlementMs = 0, drainageMs = 0;
    double waterMs = 0;
    double streamsMs = 0;
    double streamSectionsMs = 0;
    double channelPreparationMs = 0, channelCarvingMs = 0;
    double combinedWaterMs = 0;
    double playabilityMs = 0;
    // Owned vector payload only; excludes allocator overhead, generator
    // scratch, upload packing/staging, driver allocations and the rest of scene.
    size_t retainedSurfaceBytes = 0;
    size_t meshBytes = 0;
    size_t generationFieldBytes = 0; // full domain/apron retained for erosion/drainage
    size_t erosionFieldBytes = 0, erosionWorkingBytes = 0;
    size_t drainageFieldBytes = 0, drainageWorkingBytes = 0;
    size_t waterBytes = 0;
    size_t streamsBytes = 0;
    size_t streamSectionsBytes = 0;
    size_t channelCarvingBytes = 0;
    size_t combinedWaterBytes = 0;
    size_t playabilityBytes = 0;
};
struct BuildResult {
    Settings settings;
    TerrainSurface surface;
    MeshData mesh;
    Statistics statistics;
    std::optional<MacroTerrain::Fields> generationFields; // absent for legacy
    std::optional<HydraulicErosion::Result> erosion;
    std::optional<TerrainDrainage::Result> drainage;
    std::optional<LakeWater::Result> water;
    std::optional<StreamNetwork::Result> streams;
    std::optional<StreamSections::Result> streamSections;
    std::optional<ChannelCarving::Result> channelCarving;
    std::optional<TerrainWater::Result> combinedWater;
    std::optional<TerrainPlayability::Result> playability;
    std::optional<TerrainRefinement::Result> refinement;
};

MeshData buildMesh(const TerrainSurface& surface);
BuildResult build(const Settings& settings);

} // namespace TerrainGenerator
