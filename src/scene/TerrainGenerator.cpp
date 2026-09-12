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
        case Preset::DrainedValley: return "drained-valley";
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
    if (settings.refinementPasses < 0 || settings.refinementPasses > 2)
        throw std::invalid_argument("terrain refinement requires 0, 1 or 2 passes");
    if (settings.refinementPasses && settings.preset != Preset::DrainedValley)
        throw std::invalid_argument("surface refinement requires finalized drained terrain");
    if (settings.lakes && settings.preset != Preset::DrainedValley)
        throw std::invalid_argument("lake water requires finalized drained-valley terrain");
    if (settings.streamSections) {
        if (!settings.streams) throw std::invalid_argument("stream sections require stream profiles");
        StreamSections::validate(*settings.streamSections);
    }
    if (settings.channelCarving) {
        if (!settings.streams) throw std::invalid_argument("channel carving requires stream profiles");
        ChannelCarving::validate(*settings.channelCarving);
    }
    if (settings.combinedWater && !settings.streams)
        throw std::invalid_argument("combined water requires stream profiles");
    if (settings.materials) {
        if (!settings.combinedWater) throw std::invalid_argument("ground materials require final combined water");
        TerrainMaterials::validate(*settings.materials);
    }
    if (settings.outcrops) {
        if (settings.preset != Preset::DrainedValley)
            throw std::invalid_argument("rock outcrops require finalized drained-valley terrain");
        RockOutcrops::validate(*settings.outcrops);
    }
    if (settings.playability) {
        if (!settings.combinedWater) throw std::invalid_argument("playability requires final combined water");
        TerrainPlayability::validate(*settings.playability);
    }
    if (settings.streams) {
        if (!settings.lakes) throw std::invalid_argument("streams require resolved lake water");
        StreamNetwork::validate(*settings.streams);
    }
    if (settings.lakes) {
        for (double rate : {settings.lakes->evaporation, settings.lakes->seepage})
            if (!std::isfinite(rate) || rate < 0 || rate > 1)
                throw std::invalid_argument("invalid lake loss settings");
    }
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
    std::optional<RockOutcrops::Result> outcrops;
    std::optional<TerrainDrainage::Result> drainage;
    std::optional<LakeWater::Result> water;
    std::optional<StreamNetwork::Result> streams;
    std::optional<StreamSections::Result> streamSections;
    std::optional<ChannelCarving::Result> channelCarving;
    std::optional<TerrainWater::Result> combinedWater;
    std::optional<TerrainMaterials::Fields> materials;
    double channelPreparationMs = 0;
    std::optional<TerrainRefinement::Result> refinement;
    HeightmapGenerator::Heightmap hm;
    if (settings.preset != Preset::Legacy) {
        fields = MacroTerrain::generate(settings.resolution, settings.worldSize, settings.seed, settings.macro);
        if (settings.refinementPasses && fields->heightmap.resolution > 1 + 4096 / (1 << settings.refinementPasses))
            throw std::invalid_argument("refined domain including apron exceeds 4097 samples");
        if (settings.preset == Preset::ErodedValley || settings.preset == Preset::DrainedValley)
            erosion = HydraulicErosion::run(*fields, settings.erosion);
        if (settings.preset == Preset::DrainedValley) {
            HydraulicErosion::settle(*fields, *erosion);
            if (settings.refinementPasses) refinement = TerrainRefinement::apply(*fields, settings.refinementPasses);
            // Outcrops are the LAST height-changing pass before hydrology and
            // every contact/render consumer; drainage below sees final ground.
            if (settings.outcrops) outcrops = RockOutcrops::apply(*fields, settings.seed, *settings.outcrops);
            drainage = TerrainDrainage::analyze(*fields, {settings.erosion.rainfall, settings.erosion.infiltration});
            if (settings.lakes) water = LakeWater::build(*fields, *drainage, *settings.lakes);
            if (settings.streams) streams = StreamNetwork::build(*fields, *drainage, *water, *settings.streams);
            if (settings.channelCarving) {
                channelPreparationMs = drainage->elapsedMs + water->elapsedMs + streams->elapsedMs;
                auto carvingSettings = *settings.channelCarving;
                carvingSettings.smoothBanks |= settings.refinementPasses != 0;
                channelCarving = ChannelCarving::apply(*fields, *drainage, *streams, carvingSettings);
                // These results refer to pre-cut ground. Rebuild every physical
                // consumer from final fields, even if this pass made no cuts.
                drainage = TerrainDrainage::analyze(*fields, {settings.erosion.rainfall, settings.erosion.infiltration});
                water = LakeWater::build(*fields, *drainage, *settings.lakes);
                streams = StreamNetwork::build(*fields, *drainage, *water, *settings.streams);
            }
            if (settings.streamSections) streamSections = StreamSections::build(*fields, *streams, *settings.streamSections);
            if (settings.combinedWater) combinedWater = TerrainWater::build(*fields, *drainage, *water, *streams);
            if (settings.materials)
                materials = TerrainMaterials::build(*fields, *erosion, *drainage, *water, *combinedWater, *settings.materials);
        }
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
    std::optional<TerrainPlayability::Result> playability;
    if (settings.playability) playability = TerrainPlayability::analyze(combinedWater->surface, *settings.playability);
    auto analysisDone = Clock::now();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    Statistics stats;
    stats.heightfieldMs = ms(start, heightsDone);
    if (erosion) {
        stats.erosionMs = erosion->elapsedMs;
        stats.heightfieldMs -= stats.erosionMs;
        stats.erosionFieldBytes = erosion->payloadBytes();
        stats.erosionWorkingBytes = erosion->peakWorkingBytes;
        stats.settlementMs = erosion->settlementMs;
        stats.heightfieldMs -= stats.settlementMs;
    }
    if (outcrops) {
        stats.outcropsMs = outcrops->elapsedMs;
        stats.heightfieldMs -= stats.outcropsMs;
    }
    if (drainage) {
        stats.drainageMs = drainage->elapsedMs;
        stats.heightfieldMs -= stats.drainageMs;
        stats.drainageFieldBytes = drainage->payloadBytes();
        stats.drainageWorkingBytes = drainage->peakWorkingBytes;
    }
    stats.surfaceMs = ms(heightsDone, surfaceDone);
    if (water) {
        stats.waterMs = water->elapsedMs;
        stats.heightfieldMs -= stats.waterMs;
        stats.waterBytes = water->payloadBytes();
    }
    stats.meshMs = ms(surfaceDone, meshDone);
    if (streams) {
        stats.streamsMs = streams->elapsedMs;
        stats.heightfieldMs -= stats.streamsMs;
        stats.streamsBytes = streams->payloadBytes();
    }
    stats.totalMs = ms(start, analysisDone);
    stats.retainedSurfaceBytes = surface.heightmap().heights.capacity() * sizeof(float) +
                                 surface.shadingNormals().capacity() * sizeof(glm::vec3);
    stats.meshBytes = mesh.vertices.capacity() * sizeof(MeshVertex) +
                     mesh.indices.capacity() * sizeof(uint32_t);
    if (fields) stats.generationFieldBytes = fields->payloadBytes();
    if (streamSections) {
        stats.streamSectionsMs = streamSections->elapsedMs;
        stats.heightfieldMs -= stats.streamSectionsMs;
        stats.streamSectionsBytes = streamSections->payloadBytes();
    }
    if (channelCarving) {
        stats.channelPreparationMs = channelPreparationMs;
        stats.channelCarvingMs = channelCarving->elapsedMs;
        stats.channelCarvingBytes = channelCarving->payloadBytes();
        stats.heightfieldMs -= stats.channelPreparationMs + stats.channelCarvingMs;
    }
    if (combinedWater) {
        stats.combinedWaterMs = combinedWater->elapsedMs;
        stats.combinedWaterBytes = combinedWater->payloadBytes();
        stats.heightfieldMs -= stats.combinedWaterMs;
    }
    if (materials) {
        stats.materialsMs = materials->elapsedMs;
        stats.materialsBytes = materials->payloadBytes();
        stats.heightfieldMs -= stats.materialsMs;
    }
    if (playability) {
        stats.playabilityMs = playability->elapsedMs;
        stats.playabilityBytes = playability->payloadBytes();
    }
    return {settings, std::move(surface), std::move(mesh), stats, std::move(fields), std::move(erosion), std::move(outcrops), std::move(drainage), std::move(water), std::move(streams), std::move(streamSections), std::move(channelCarving), std::move(combinedWater), std::move(materials), std::move(playability), std::move(refinement)};
}

} // namespace TerrainGenerator
