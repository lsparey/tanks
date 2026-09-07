#include "TerrainGenerator.h"

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace TerrainGenerator {

const char* presetName(Preset preset) {
    switch (preset) {
        case Preset::Legacy: return "legacy";
        case Preset::RollingValley: return "rolling-valley";
        case Preset::ErodedValley: return "eroded-valley";
    }
    throw std::invalid_argument("unsupported terrain preset");
}

MeshData buildMesh(const TerrainSurface& surface) {
    const auto& hm = surface.heightmap();
    MeshData mesh;
    mesh.vertices.resize(hm.heights.size());
    mesh.indices.reserve(static_cast<size_t>(hm.resolution - 1) * (hm.resolution - 1) * 6);
    for (int z = 0; z < hm.resolution; ++z) {
        for (int x = 0; x < hm.resolution; ++x) {
            size_t i = static_cast<size_t>(z) * hm.resolution + x;
            glm::vec3 p = surface.position(x, z);
            mesh.vertices[i] = {p, surface.shadingNormals()[i], glm::vec2(p.x, p.z) / 3.0f};
        }
    }
    for (int z = 0; z < hm.resolution - 1; ++z) {
        for (int x = 0; x < hm.resolution - 1; ++x) {
            auto indices = TerrainSurface::quadIndices(hm.resolution, x, z);
            mesh.indices.insert(mesh.indices.end(), indices.begin(), indices.end());
        }
    }
    return mesh;
}

BuildResult build(const Settings& settings) {
    presetName(settings.preset); // reject unknown enum values before allocating
    if (settings.version != kVersion)
        throw std::invalid_argument("unsupported terrain generator version");
    // Bound before allocation and before legacy noise converts world positions
    // to integer lattice coordinates. This is an input limit, not a RAM budget.
    if (settings.resolution < 2 || settings.resolution > 4097 ||
        !std::isfinite(settings.worldSize) || settings.worldSize < 0.001f ||
        settings.worldSize > 1000000.0f || !std::isfinite(settings.amplitude) ||
        settings.amplitude < 0 || settings.amplitude > 1000000.0f)
        throw std::invalid_argument("invalid terrain generation settings");
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    std::optional<MacroTerrain::Fields> fields;
    std::optional<HydraulicErosion::Result> erosion;
    HeightmapGenerator::Heightmap hm;
    if (settings.preset != Preset::Legacy) {
        fields = MacroTerrain::generate(settings.resolution, settings.worldSize, settings.seed, settings.macro);
        if (settings.preset == Preset::ErodedValley) erosion = HydraulicErosion::run(*fields, settings.erosion);
        hm = fields->crop();
    } else {
        hm = HeightmapGenerator::generateHills(settings.resolution, settings.worldSize,
                                               settings.amplitude, settings.seed);
    }
    auto heightsDone = Clock::now();
    TerrainSurface surface(std::move(hm));
    auto surfaceDone = Clock::now();
    auto mesh = buildMesh(surface);
    auto meshDone = Clock::now();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    Statistics stats;
    stats.heightfieldMs = ms(start, heightsDone);
    if (erosion) {
        stats.erosionMs = erosion->elapsedMs;
        stats.heightfieldMs -= stats.erosionMs;
        stats.erosionFieldBytes = erosion->payloadBytes();
        stats.erosionWorkingBytes = erosion->peakWorkingBytes;
    }
    stats.surfaceMs = ms(heightsDone, surfaceDone);
    stats.meshMs = ms(surfaceDone, meshDone);
    stats.totalMs = ms(start, meshDone);
    stats.retainedSurfaceBytes = surface.heightmap().heights.capacity() * sizeof(float) +
                                 surface.shadingNormals().capacity() * sizeof(glm::vec3);
    stats.meshBytes = mesh.vertices.capacity() * sizeof(MeshVertex) +
                     mesh.indices.capacity() * sizeof(uint32_t);
    if (fields) stats.generationFieldBytes = fields->payloadBytes();
    return {settings, std::move(surface), std::move(mesh), stats, std::move(fields), std::move(erosion)};
}

} // namespace TerrainGenerator
