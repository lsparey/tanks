#pragma once

#include <cstddef>
#include <optional>
#include "MacroTerrain.h"
#include "HydraulicErosion.h"
#include "TerrainSurface.h"

namespace TerrainGenerator {

// Recipe version is scoped by preset. Legacy v1 stays available for comparison.
inline constexpr uint32_t kVersion = 1;
enum class Preset { Legacy, RollingValley, ErodedValley };
const char* presetName(Preset preset);
inline constexpr std::array<uint32_t, 16> kRegressionSeeds{
    7331, 0, 1, 2, 7, 42, 123, 256, 1024, 4096, 12345, 65535,
    99991, 1000003, 2147483648u, 4294967295u};

struct Settings {
    Preset preset = Preset::Legacy;
    uint32_t version = kVersion;
    uint32_t seed = 7331;
    int resolution = 256;
    float worldSize = 180.0f;
    float amplitude = 2.2f; // legacy recipe only
    MacroTerrain::Settings macro; // rolling-valley recipe only
    HydraulicErosion::Settings erosion; // eroded-valley prototype only
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
    // Owned vector payload only; excludes allocator overhead, generator
    // scratch, upload packing/staging, driver allocations and the rest of scene.
    size_t retainedSurfaceBytes = 0;
    size_t meshBytes = 0;
    size_t generationFieldBytes = 0; // full domain/apron retained for erosion/drainage
    size_t erosionFieldBytes = 0, erosionWorkingBytes = 0;
};
struct BuildResult {
    Settings settings;
    TerrainSurface surface;
    MeshData mesh;
    Statistics statistics;
    std::optional<MacroTerrain::Fields> generationFields; // absent for legacy
    std::optional<HydraulicErosion::Result> erosion;
};

MeshData buildMesh(const TerrainSurface& surface);
BuildResult build(const Settings& settings);

} // namespace TerrainGenerator
