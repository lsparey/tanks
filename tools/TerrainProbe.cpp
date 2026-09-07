#include "scene/TerrainGenerator.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
template<class T> T number(std::string_view value) {
    T result{};
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument("invalid numeric argument");
    return result;
}

uint64_t fingerprint(const TerrainGenerator::BuildResult& build) {
    uint64_t hash = 14695981039346656037ull;
    auto word = [&](uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            hash ^= (value >> (8 * i)) & 255u;
            hash *= 1099511628211ull;
        }
    };
    for (const auto& v : build.mesh.vertices) {
        for (float f : {v.position.x, v.position.y, v.position.z,
                        v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y})
            word(std::bit_cast<uint32_t>(f));
    }
    for (uint32_t index : build.mesh.indices) word(index);
    return hash;
}

uint64_t fieldFingerprint(const TerrainGenerator::BuildResult& build) {
    if (!build.generationFields) return 0;
    uint64_t hash = 14695981039346656037ull;
    auto word = [&](uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            hash ^= (value >> (8 * i)) & 255u;
            hash *= 1099511628211ull;
        }
    };
    if (build.refinement) {
        word(TerrainRefinement::kVersion);
        word(build.refinement->sourceResolution);
        word(build.refinement->targetResolution);
    }
    const auto& fields = *build.generationFields;
    word(fields.heightmap.resolution);
    word(fields.playableResolution);
    word(fields.apronCells);
    word(std::bit_cast<uint32_t>(fields.spacing));
    for (const auto* values : {&fields.heightmap.heights, &fields.bedrock, &fields.soil, &fields.erodibility})
        for (float value : *values) word(std::bit_cast<uint32_t>(value));
    for (auto value : fields.openFaces) word(value);
    if (build.erosion) {
        const auto& e = *build.erosion;
        for (const auto* values : {&e.water, &e.sediment, &e.erosion, &e.deposition, &e.waterExposure, &e.throughflow, &e.relaxation}) {
            for (double value : *values) {
                uint64_t bits = std::bit_cast<uint64_t>(value);
                word(uint32_t(bits)); word(uint32_t(bits >> 32));
            }
        }
        word(e.steps);
        if (e.finalized) word(1); // retain historical hashes for raw erosion v1
    }
    if (build.drainage) {
        const auto& d = *build.drainage;
        for (float value : d.spillElevation) word(std::bit_cast<uint32_t>(value));
        for (const auto* values : {&d.downstream, &d.basin})
            for (auto value : *values) word(uint32_t(value));
        for (const auto* values : {&d.order, &d.outlet})
            for (auto value : *values) word(value);
        for (const auto* values : {&d.contributingArea, &d.runoff}) {
            for (double value : *values) {
                uint64_t bits = std::bit_cast<uint64_t>(value);
                word(uint32_t(bits)); word(uint32_t(bits >> 32));
            }
        }
    }
    if (build.water) {
        word(LakeWater::kVersion);
        const auto& w = *build.water;
        auto scalar = [&](double value) {
            uint64_t bits = std::bit_cast<uint64_t>(value);
            word(uint32_t(bits)); word(uint32_t(bits >> 32));
        };
        for (const auto& lake : w.lakes) {
            word(lake.present); word(std::bit_cast<uint32_t>(lake.level));
            word(uint32_t(lake.spillFrom)); word(uint32_t(lake.spillTo));
            for (double value : {lake.area, lake.volume, lake.inflow, lake.loss, lake.outflow}) scalar(value);
        }
        for (double q : w.discharge) scalar(q);
        for (int32_t b : w.surface.triangleLakes()) word(uint32_t(b));
        for (const auto& v : w.surface.mesh().vertices)
            for (float f : {v.position.x, v.position.y, v.position.z, v.depth}) word(std::bit_cast<uint32_t>(f));
        for (uint32_t i : w.surface.mesh().indices) word(i);
        for (const auto& shore : w.surface.shores())
            for (float f : {shore.a.x, shore.a.y, shore.b.x, shore.b.y}) word(std::bit_cast<uint32_t>(f));
    }
    if (build.streams) {
        word(StreamNetwork::kVersion);
        auto scalar = [&](double value) {
            uint64_t bits = std::bit_cast<uint64_t>(value);
            word(uint32_t(bits)); word(uint32_t(bits >> 32));
        };
        const auto& s = *build.streams;
        for (const auto& node : s.nodes) {
            for (uint32_t value : {node.cell, uint32_t(node.kind), uint32_t(node.lake), uint32_t(node.downstream), node.incoming}) word(value);
            scalar(node.discharge);
            for (float f : {node.position.x, node.position.y, node.flow.x, node.flow.y, node.ground, node.waterLevel,
                            node.width, node.requestedDepth, node.availableDepth, node.depthDeficit}) word(std::bit_cast<uint32_t>(f));
        }
        for (const auto* values : {&s.downstreamOrder, &s.reachNodes}) for (uint32_t value : *values) word(value);
        for (const auto& reach : s.reaches) { word(reach.first); word(reach.count); scalar(reach.length); }
    }
    if (build.streamSections) {
        word(StreamSections::kVersion);
        auto scalar = [&](double value) {
            uint64_t bits = std::bit_cast<uint64_t>(value);
            word(uint32_t(bits)); word(uint32_t(bits >> 32));
        };
        const auto& r = *build.streamSections;
        for (const auto& s : r.sections) {
            word(s.from); word(s.to);
            for (double v : {s.station, s.centre.x, s.centre.y, s.leftDirection.x, s.leftDirection.y,
                             s.waterLevel, s.ground, s.requestedWidth}) scalar(v);
            for (const auto* b : {&s.left, &s.right}) {
                word(uint32_t(b->end)); word(b->first); word(b->count);
                scalar(b->distance); scalar(b->area); scalar(b->wettedPerimeter);
            }
        }
        for (const auto& p : r.points) { scalar(p.distance); scalar(p.ground); }
        for (const auto& c : r.controls) { word(c.from); word(c.to); word(c.dryFrom); word(c.dryTo); }
    }
    if (build.channelCarving) {
        word(ChannelCarving::kVersion);
        const auto& r = *build.channelCarving;
        for (float value : r.cutDepth) word(std::bit_cast<uint32_t>(value));
        for (uint8_t value : r.protectedCells) word(value);
        const auto& b = r.budget;
        for (double value : {b.initialSoil, b.finalSoil, b.exportedSoil, b.exportedBedrock,
                             b.removedGround, b.materialResidual, b.surfaceRoundingDelta}) {
            uint64_t bits = std::bit_cast<uint64_t>(value);
            word(uint32_t(bits)); word(uint32_t(bits >> 32));
        }
    }
    if (build.combinedWater) {
        word(TerrainWater::kVersion);
        const auto& r = *build.combinedWater;
        for (float v : r.streamLevels) word(std::bit_cast<uint32_t>(v));
        for (uint8_t v : r.connected) word(v);
        for (const auto& v : r.surface.mesh().vertices)
            for (float f : {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z,
                            v.flow.x, v.flow.y, v.depth}) word(std::bit_cast<uint32_t>(f));
        for (uint32_t i : r.surface.mesh().indices) word(i);
        for (const auto& s : r.surface.shores())
            for (float f : {s.a.x, s.a.y, s.b.x, s.b.y}) word(std::bit_cast<uint32_t>(f));
        for (double v : {r.streamArea, r.lakeArea}) {
            uint64_t bits = std::bit_cast<uint64_t>(v);
            word(uint32_t(bits)); word(uint32_t(bits >> 32));
        }
    }
    if (build.playability) {
        const auto& r = *build.playability;
        word(TerrainPlayability::kVersion); word(uint32_t(r.status)); word(uint32_t(r.resolution));
        auto scalar = [&](double value) {
            uint64_t bits = std::bit_cast<uint64_t>(value);
            word(uint32_t(bits)); word(uint32_t(bits >> 32));
        };
        for (const auto* values : {&r.terrainFlags, &r.flags}) for (uint8_t v : *values) word(v);
        for (int32_t v : r.component) word(uint32_t(v));
        for (const auto& c : r.components) { word(c.first); word(c.cells); scalar(c.area); }
        for (uint32_t v : r.route) word(v);
        for (double v : {r.footprintRadius, r.boundaryHalfExtent, r.routeLength, r.routeSpan}) scalar(v);
        word(r.spawn.has_value());
        if (r.spawn) {
            word(r.spawn->cell); word(r.spawn->component);
            for (float v : {r.spawn->position.x, r.spawn->position.y, r.spawn->position.z, r.spawn->forward.x, r.spawn->forward.y})
                word(std::bit_cast<uint32_t>(v));
        }
    }
    return hash;
}

void exportDiagnostics(const TerrainGenerator::BuildResult& build, const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto& hm = build.surface.heightmap();
    std::string stem = std::string(TerrainGenerator::presetName(build.settings.preset)) + "-v" +
                       std::to_string(build.settings.version) + "-seed-" +
                       std::to_string(build.settings.seed) + "-" + std::to_string(hm.resolution);
    if (build.settings.refinementPasses) stem += "-refinement-v1";
    if (build.generationFields && build.settings.macro.landform != MacroTerrain::Landform::Valley)
        stem += "-" + std::string(MacroTerrain::landformName(build.settings.macro.landform)) +
                "-landform-v" + std::to_string(MacroTerrain::kLandformVersion);
    if (build.water) stem += "-lakes-v" + std::to_string(LakeWater::kVersion);
    if (build.streams) stem += "-streams-v" + std::to_string(StreamNetwork::kVersion);
    if (build.streamSections) stem += "-sections-v" + std::to_string(StreamSections::kVersion);
    if (build.channelCarving) stem += "-carved-v" + std::to_string(ChannelCarving::kVersion);
    if (build.combinedWater) stem += "-water-v" + std::to_string(TerrainWater::kVersion);
    if (build.playability) stem += "-play-v" + std::to_string(TerrainPlayability::kVersion);
    auto open = [&](std::string_view suffix) {
        std::ofstream file(directory / (stem + std::string(suffix)), std::ios::binary);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        return file;
    };
    auto obj = open(".obj");
    obj << std::setprecision(9) << "# CPU terrain; world units, +Y up, no vertical exaggeration\n";
    for (const auto& v : build.mesh.vertices)
        obj << "v " << v.position.x << ' ' << v.position.y << ' ' << v.position.z << '\n';
    for (const auto& v : build.mesh.vertices)
        obj << "vn " << v.normal.x << ' ' << v.normal.y << ' ' << v.normal.z << '\n';
    for (size_t i = 0; i < build.mesh.indices.size(); i += 3) {
        obj << 'f';
        for (size_t k = 0; k < 3; ++k) {
            auto index = build.mesh.indices[i + k] + 1;
            obj << ' ' << index << "//" << index;
        }
        obj << '\n';
    }
    obj.close();

    auto neutral = open("-neutral.ppm");
    auto heights = open("-height.pgm");
    neutral << "P6\n" << hm.resolution << ' ' << hm.resolution << "\n255\n";
    heights << "P5\n" << hm.resolution << ' ' << hm.resolution << "\n65535\n";
    auto [minimum, maximum] = std::minmax_element(hm.heights.begin(), hm.heights.end());
    float span = std::max(*maximum - *minimum, 1e-6f);
    glm::vec3 light = glm::normalize(glm::vec3(-.6f, 1.0f, -.4f));
    for (size_t i = 0; i < hm.heights.size(); ++i) {
        // Fixed neutral Lambert hillshade, no texture, AO, props or water.
        float shade = .2f + .7f * std::max(0.0f, glm::dot(build.surface.shadingNormals()[i], light));
        auto pixel = static_cast<unsigned char>(std::clamp(shade * 255.0f, 0.0f, 255.0f));
        for (int c = 0; c < 3; ++c) neutral.put(static_cast<char>(pixel));
        auto height = static_cast<uint16_t>((hm.heights[i] - *minimum) / span * 65535.0f);
        heights.put(static_cast<char>(height >> 8));
        heights.put(static_cast<char>(height & 255));
    }
    neutral.close();
    heights.close();
    auto metadata = open(".txt");
    metadata << std::setprecision(9)
             << "generator=" << TerrainGenerator::presetName(build.settings.preset)
             << "\nversion=" << build.settings.version
             << "\nseed=" << build.settings.seed << "\nresolution=" << hm.resolution
             << "\nworld_size=" << hm.worldSize
             << "\nspacing=" << hm.worldSize / (hm.resolution - 1)
             << "\nheight_min=" << *minimum << "\nheight_max=" << *maximum
             << "\nmesh_fnv1a64=" << std::hex << fingerprint(build) << std::dec
             << "\nview=orthographic top-down; left=-X, top=-Z; no vertical exaggeration"
             << "\nlight=normalize(-0.6,1,-0.4); shade=0.2+0.7*max(dot(normal,light),0)"
             << "\nheight_pgm=16-bit big-endian, normalized from height_min to height_max"
             << "\nbackend=CPU; build="
#ifdef NDEBUG
             << "Release"
#else
             << "Debug"
#endif
             << "; compiler=" << __VERSION__ << '\n';
    if (!build.generationFields) metadata << "amplitude=" << build.settings.amplitude << '\n';
    if (build.refinement) {
        metadata << "refinement_version=" << TerrainRefinement::kVersion
                 << "\nrefinement_passes=" << build.settings.refinementPasses
                 << "\nrefinement_ms=" << build.refinement->elapsedMs
                 << "\nrefinement_soil_volume_delta=" << build.refinement->soilVolumeDelta
                 << "\nrefinement_bedrock_volume_delta=" << build.refinement->bedrockVolumeDelta << '\n';
    }
    if (build.generationFields) {
        const auto& fields = *build.generationFields;
        auto fieldImage = [&](std::string_view suffix, const auto& values, double low, double high, int resolution = 0) {
            auto file = open(suffix);
            if (!resolution) resolution = fields.heightmap.resolution;
            if (values.size() != size_t(resolution) * resolution) throw std::runtime_error("diagnostic grid mismatch");
            file << "P5\n" << resolution << ' ' << resolution << "\n65535\n";
            for (double value : values) {
                auto pixel = static_cast<uint16_t>(std::clamp((value - low) / std::max(high - low, 1e-12), 0.0, 1.0) * 65535);
                file.put(static_cast<char>(pixel >> 8));
                file.put(static_cast<char>(pixel & 255));
            }
            file.close();
        };
        auto [groundMin, groundMax] = std::minmax_element(fields.heightmap.heights.begin(), fields.heightmap.heights.end());
        auto [rockMin, rockMax] = std::minmax_element(fields.bedrock.begin(), fields.bedrock.end());
        float soilMax = *std::max_element(fields.soil.begin(), fields.soil.end());
        fieldImage("-domain-height.pgm", fields.heightmap.heights, *groundMin, *groundMax);
        if (build.channelCarving) {
            fieldImage("-channel-cut.pgm", build.channelCarving->cutDepth, 0, build.settings.channelCarving->maximumCut);
            fieldImage("-channel-protected.pgm", build.channelCarving->protectedCells, 0, 1);
        }
        fieldImage("-bedrock.pgm", fields.bedrock, *rockMin, *rockMax);
        fieldImage("-soil.pgm", fields.soil, 0, soilMax);
        fieldImage("-erodibility.pgm", fields.erodibility, 0, 1);
        auto outlets = open("-outlets.pgm");
        outlets << "P5\n" << fields.heightmap.resolution << ' ' << fields.heightmap.resolution << "\n255\n";
        for (uint8_t value : fields.openFaces) outlets.put(static_cast<char>(value));
        outlets.close();
        metadata << "landform_version=" << MacroTerrain::kLandformVersion
                 << "\nlandform_requested=" << MacroTerrain::landformName(build.settings.macro.landform)
                 << "\nlandform_resolved=" << MacroTerrain::landformName(MacroTerrain::resolveLandform(build.settings.macro.landform, build.settings.seed))
                 << "\nwarp_strength=" << build.settings.macro.warpStrength
                 << "\nrelief=" << build.settings.macro.relief << "\nvalley_width=" << build.settings.macro.valleyWidth
                 << "\nfeature_scale=" << build.settings.macro.featureScale << "\nsoil_depth=" << build.settings.macro.soilDepth
                 << "\napron_requested=" << build.settings.macro.apronWidth << "\napron_cells=" << fields.apronCells
                 << "\napron_actual=" << fields.apronCells * fields.spacing
                 << "\ndomain_resolution=" << fields.heightmap.resolution << "\ndomain_world_size=" << fields.heightmap.worldSize
                 << "\ndomain_height_min=" << *groundMin << "\ndomain_height_max=" << *groundMax
                 << "\nbedrock_min=" << *rockMin << "\nbedrock_max=" << *rockMax << "\nsoil_max=" << soilMax
                 << "\nerodibility_range=0,1\noutlet_bits=1:-X,2:+X,4:-Z,8:+Z; raw 8-bit values"
                 << "\nfield_fnv1a64=" << std::hex << fieldFingerprint(build) << std::dec << '\n';
        if (build.erosion) {
            const auto& e = *build.erosion;
            const auto& s = build.settings.erosion;
            int erosionResolution = build.refinement ? build.refinement->sourceResolution : fields.heightmap.resolution;
            metadata << "erosion_domain_resolution=" << erosionResolution << '\n';
            struct Diagnostic { const char* name; const std::vector<double>* values; };
            for (const auto& diagnostic : {Diagnostic{"erosion", &e.erosion}, {"deposition", &e.deposition},
                                           {"water", &e.water}, {"sediment", &e.sediment},
                                           {"water-exposure", &e.waterExposure}, {"throughflow", &e.throughflow}}) {
                double maximum = *std::max_element(diagnostic.values->begin(), diagnostic.values->end());
                fieldImage(std::string("-") + diagnostic.name + ".pgm", *diagnostic.values, 0, maximum, erosionResolution);
                metadata << diagnostic.name << "_max=" << maximum << '\n';
            }
            std::vector<double> change(e.water.size());
            double magnitude = 0;
            for (size_t i = 0; i < change.size(); ++i) {
                change[i] = e.deposition[i] - e.erosion[i] + e.relaxation[i];
                magnitude = std::max(magnitude, std::abs(change[i]));
            }
            fieldImage("-height-change.pgm", change, -magnitude, magnitude, erosionResolution);
            metadata << "height_change_range=" << -magnitude << ',' << magnitude << '\n';
            const auto& b = e.budget;
            metadata << std::setprecision(17)
                     << "erosion_duration=" << s.duration << "\nrain_duration=" << s.rainDuration
                     << "\nmax_timestep=" << s.maxTimestep << "\ncfl=" << s.cfl << "\nmax_steps=" << s.maxSteps
                     << "\nrainfall_rate=" << s.rainfall << "\ninfiltration_rate=" << s.infiltration
                     << "\nevaporation_rate=" << s.evaporation << "\ndrag=" << s.drag << "\ncapacity=" << s.capacity
                     << "\nmax_concentration=" << s.maxConcentration << "\nerosion_workers=" << s.workers
                     << "\nerosion_rate=" << s.erosionRate << "\ndeposition_rate=" << s.depositionRate
                     << "\nbedrock_rate=" << s.bedrockRate << "\nmax_change_rate=" << s.maxChangeRate
                     << "\ntalus_passes=" << s.talusPasses << "\ntalus_slope=" << s.talusSlope
                     << "\ntalus_fraction=" << s.talusFraction << "\nmax_talus_depth=" << s.maxTalusDepth
                     << "\nerosion_steps=" << e.steps << "\nsimulated_seconds=" << e.simulatedSeconds
                     << "\nerosion_ms=" << e.elapsedMs << "\nerosion_working_bytes=" << e.peakWorkingBytes
                     << "\ninitial_water=" << b.initialWater << "\nrainfall_volume=" << b.rainfall
                     << "\ninfiltration_volume=" << b.infiltration << "\nevaporation_volume=" << b.evaporation
                     << "\nexported_water=" << b.exportedWater << "\nfinal_water=" << b.finalWater
                     << "\nwater_residual=" << b.waterResidual << "\ninitial_soil=" << b.initialSoil
                     << "\ninitial_sediment=" << b.initialSediment << "\nconverted_bedrock=" << b.convertedBedrock
                     << "\nexported_sediment=" << b.exportedSediment << "\nfinal_soil=" << b.finalSoil
                     << "\nfinal_sediment=" << b.finalSediment << "\nsolid_residual=" << b.solidResidual
                     << "\nsolid_rounding_delta=" << b.solidRoundingDelta
                     << "\nfinalized=" << e.finalized << "\nsettlement_ms=" << e.settlementMs
                     << "\nremoved_transient_water=" << b.removedTransientWater
                     << "\nsettled_sediment=" << b.settledSediment << '\n';
        }
        if (build.drainage) {
            const auto& d = *build.drainage;
            std::vector<double> spillDepth(d.spillElevation.size()), basinLabels(d.basin.size());
            std::vector<uint32_t> rank(d.order.size());
            for (size_t i = 0; i < spillDepth.size(); ++i) {
                spillDepth[i] = double(d.spillElevation[i]) - fields.heightmap.heights[i];
                basinLabels[i] = d.basin[i] + 1;
                rank[d.order[i]] = uint32_t(i);
            }
            auto maximum = [](const auto& values) { return *std::max_element(values.begin(), values.end()); };
            fieldImage("-spill-depth.pgm", spillDepth, 0, maximum(spillDepth));
            fieldImage("-contributing-area.pgm", d.contributingArea, 0, maximum(d.contributingArea));
            fieldImage("-runoff.pgm", d.runoff, 0, maximum(d.runoff));
            fieldImage("-basins.pgm", basinLabels, 0, double(d.basins.size()));
            auto routing = open("-drainage.csv");
            routing << std::setprecision(17)
                    << "index,x,z,ground,spill_elevation,downstream,rank,outlet,basin,contributing_area,runoff\n";
            for (size_t i = 0; i < d.order.size(); ++i)
                routing << i << ',' << i % fields.heightmap.resolution << ',' << i / fields.heightmap.resolution << ','
                        << fields.heightmap.heights[i] << ',' << d.spillElevation[i] << ',' << d.downstream[i] << ','
                        << rank[i] << ',' << d.outlet[i] << ',' << d.basin[i] << ',' << d.contributingArea[i] << ',' << d.runoff[i] << '\n';
            routing.close();
            auto basins = open("-basins.csv");
            basins << std::setprecision(17)
                   << "basin,cells,spill_elevation,minimum_ground,area,storage_to_spill,outlet_links,spill_from,spill_to\n";
            double storage = 0;
            for (size_t i = 0; i < d.basins.size(); ++i) {
                const auto& b = d.basins[i];
                storage += b.storageToSpill;
                basins << i << ',' << b.cells << ',' << b.spillElevation << ',' << b.minimumGround << ','
                       << b.area << ',' << b.storageToSpill << ',' << b.outletLinks << ',' << b.spillFrom << ',' << b.spillTo << '\n';
            }
            basins.close();
            metadata << std::setprecision(17)
                     << "drainage=final-ground analysis only; no persistent water or spawn guarantee"
                     << "\ndrainage_neighbours=terrain mesh edges: cardinal and NW/SE diagonal"
                     << "\ndrainage_ties=FIFO discovery; steepest descending edge; parent on flats"
                     << "\nbasin_policy=connected ground below external escape level; not nested partial lakes"
                     << "\nrunoff_policy=max(rainfall-infiltration,0)*dual_area; potential overflowing supply"
                     << "\ndrainage_ms=" << d.elapsedMs << "\ndrainage_field_bytes=" << d.payloadBytes()
                     << "\ndrainage_working_bytes=" << d.peakWorkingBytes
                     << "\ndomain_area=" << d.domainArea << "\ngenerated_runoff=" << d.generatedRunoff
                     << "\noutlet_runoff=" << d.outletRunoff << "\nrunoff_residual=" << d.runoffResidual
                     << "\nbasin_count=" << d.basins.size() << "\nbasin_storage_to_spill=" << storage
                     << "\nspill_depth_max=" << maximum(spillDepth)
                     << "\ncontributing_area_max=" << maximum(d.contributingArea) << "\nrunoff_max=" << maximum(d.runoff)
                     << "\nbasins_pgm=label+1 normalized to basin_count; 0 means no depression"
                     << "\nspill_depth_pgm=escape elevation minus ground, NOT permanent water depth"
                     << "\ndrainage_csv=full-domain vertices; downstream -1 denotes an explicit exterior outlet\n";
        }
    }
    if (build.water) {
        const auto& w = *build.water;
        const auto& mesh = w.surface.mesh();
        auto lakeObj = open("-lake-water.obj");
        lakeObj << std::setprecision(9) << "# Exact clipped CPU lake mesh; +Y up; playable crop\n";
        for (const auto& v : mesh.vertices)
            lakeObj << "v " << v.position.x << ' ' << v.position.y << ' ' << v.position.z << '\n';
        lakeObj << "vn 0 1 0\n";
        for (size_t i = 0; i < mesh.indices.size(); i += 3)
            lakeObj << "f " << mesh.indices[i] + 1 << "//1 " << mesh.indices[i + 1] + 1 << "//1 "
                    << mesh.indices[i + 2] + 1 << "//1\n";
        lakeObj.close();
        auto lakes = open("-lakes.csv");
        lakes << std::setprecision(17) << "basin,present,level,full_area,full_volume,inflow,loss,outflow,spill_from,spill_to\n";
        size_t present = 0;
        for (size_t b = 0; b < w.lakes.size(); ++b) {
            const auto& lake = w.lakes[b];
            present += lake.present;
            lakes << b << ',' << lake.present << ',' << lake.level << ',' << lake.area << ',' << lake.volume << ','
                  << lake.inflow << ',' << lake.loss << ',' << lake.outflow << ',' << lake.spillFrom << ',' << lake.spillTo << '\n';
        }
        lakes.close();
        auto samples = open("-water-samples.csv");
        samples << std::setprecision(9) << "x,z,lake,water_height,depth,shoreline_distance\n";
        for (int z = 0; z < hm.resolution; ++z) {
            for (int x = 0; x < hm.resolution; ++x) {
                auto p = build.surface.position(x, z);
                auto sample = w.surface.sampleAt(p.x, p.z);
                auto distance = w.surface.shorelineDistanceAt(p.x, p.z);
                samples << p.x << ',' << p.z << ',' << (sample ? int32_t(sample->lake) : -1) << ',';
                if (sample) samples << sample->height;
                samples << ',' << (sample ? sample->depth : 0) << ',';
                if (distance) samples << *distance;
                samples << '\n';
            }
        }
        samples.close();
        auto flux = open("-resolved-runoff.csv");
        flux << std::setprecision(17) << "index,discharge\n";
        for (size_t i = 0; i < w.discharge.size(); ++i) flux << i << ',' << w.discharge[i] << '\n';
        flux.close();
        metadata << std::setprecision(17) << "lake_water_version=" << LakeWater::kVersion
                 << "\nlake_policy=full at spill when supplied, otherwise dry sink; no nested partial fills or streams"
                 << "\nlake_evaporation=" << build.settings.lakes->evaporation << "\nlake_seepage=" << build.settings.lakes->seepage
                 << "\nlakes_present=" << present << "\nwater_vertices=" << mesh.vertices.size()
                 << "\nwater_triangles=" << mesh.indices.size() / 3 << "\nshore_segments=" << w.surface.shores().size()
                 << "\nshore_index_bytes=" << w.surface.shorelineIndexBytes()
                 << "\nlake_generated_runoff=" << w.generatedRunoff << "\nlake_basin_loss=" << w.basinLoss
                 << "\nlake_exported_runoff=" << w.exportedRunoff << "\nlake_runoff_residual=" << w.runoffResidual
                 << "\nwater_ms=" << w.elapsedMs << "\nwater_bytes=" << w.payloadBytes()
                 << "\nwater_samples=playable grid; dry height blank; no contour distance blank; flow zero for lakes"
                 << "\nshoreline=exact distance to full-domain contours; negative in water; crop edge is not a bank\n";
    }
    if (build.streams) {
        const auto& s = *build.streams;
        const auto& settings = *build.settings.streams;
        auto nodes = open("-stream-nodes.csv");
        nodes << std::setprecision(17)
              << "node,cell,kind,lake,downstream,incoming,discharge,x,z,ground,water_level,width,requested_depth,available_depth,depth_deficit,flow_x,flow_z\n";
        auto guides = open("-stream-guides.obj");
        guides << std::setprecision(9) << "# Full-apron stream design guides, NOT a water mesh. No faces or terrain edits.\n";
        for (size_t i = 0; i < s.nodes.size(); ++i) {
            const auto& n = s.nodes[i];
            nodes << i << ',' << n.cell << ',' << StreamNetwork::kindName(n.kind) << ',' << n.lake << ','
                  << n.downstream << ',' << n.incoming << ',' << n.discharge << ',' << n.position.x << ',' << n.position.y << ','
                  << n.ground << ',' << n.waterLevel << ',' << n.width << ',' << n.requestedDepth << ',' << n.availableDepth << ','
                  << n.depthDeficit << ',' << n.flow.x << ',' << n.flow.y << '\n';
            guides << "v " << n.position.x << ' ' << n.waterLevel << ' ' << n.position.y << '\n';
        }
        nodes.close();
        auto reaches = open("-stream-reaches.csv");
        reaches << std::setprecision(17) << "reach,station,node,distance\n";
        for (size_t i = 0; i < s.reaches.size(); ++i) {
            const auto& reach = s.reaches[i];
            double distance = 0;
            guides << 'l';
            for (uint32_t k = 0; k < reach.count; ++k) {
                uint32_t node = s.reachNodes[reach.first + k];
                if (k > 0) distance += glm::length(glm::dvec2(s.nodes[node].position) -
                    glm::dvec2(s.nodes[s.reachNodes[reach.first + k - 1]].position));
                reaches << i << ',' << k << ',' << node << ',' << distance << '\n';
                guides << ' ' << node + 1;
            }
            guides << '\n';
        }
        reaches.close(); guides.close();
        metadata << std::setprecision(17) << "stream_version=" << StreamNetwork::kVersion
                 << "\nstream_policy=resolved lake discharge; reservoir inlet/outlet split; fixed lake caps plus upstream backwater"
                 << "\nstream_geometry=design guides only; no stream water mesh, water queries or bed edits"
                 << "\nstream_min_discharge=" << settings.minimumDischarge << "\nstream_width_at_threshold=" << settings.widthAtThreshold
                 << "\nstream_max_width=" << settings.maximumWidth << "\nstream_depth_at_threshold=" << settings.depthAtThreshold
                 << "\nstream_max_depth=" << settings.maximumDepth << "\nstream_nodes=" << s.nodes.size()
                 << "\nstream_reaches=" << s.reaches.size() << "\nstream_confluences=" << s.confluences
                 << "\nstream_deficient_nodes=" << s.deficientNodes << "\nstream_max_deficit=" << s.maximumDeficit
                 << "\nstream_ms=" << s.elapsedMs << "\nstream_bytes=" << s.payloadBytes()
                 << "\nstream_width_rule=min(max_width,width_at_threshold*sqrt(discharge/threshold))"
                 << "\nstream_depth_rule=min(max_depth,depth_at_threshold*cbrt(discharge/threshold))"
                 << "\nstream_deficit=longitudinal requested minus available depth; not a bank survey or approved excavation\n";
    }
    if (build.streamSections) {
        const auto& r = *build.streamSections;
        auto sections = open("-stream-sections.csv");
        sections << std::setprecision(17)
                 << "section,from,to,station,x,z,left_x,left_z,water_level,ground,requested_width,left_end,right_end,left_distance,right_distance,area,wetted_perimeter,bounded\n";
        auto points = open("-section-points.csv");
        points << std::setprecision(17) << "section,side,distance,ground\n";
        auto lines = open("-stream-sections.obj");
        lines << std::setprecision(17) << "# Exact terrain cross-sections, NOT connected stream water geometry. Full apron.\n";
        uint32_t vertex = 1;
        for (size_t i = 0; i < r.sections.size(); ++i) {
            const auto& s = r.sections[i];
            sections << i << ',' << s.from << ',' << s.to << ',' << s.station << ',' << s.centre.x << ',' << s.centre.y << ','
                     << s.leftDirection.x << ',' << s.leftDirection.y << ',' << s.waterLevel << ',' << s.ground << ',' << s.requestedWidth << ','
                     << StreamSections::endName(s.left.end) << ',' << StreamSections::endName(s.right.end) << ','
                     << s.left.distance << ',' << s.right.distance << ',' << s.left.area + s.right.area << ','
                     << s.left.wettedPerimeter + s.right.wettedPerimeter << ',' << s.bounded() << '\n';
            for (int side = 0; side < 2; ++side) {
                const auto& b = side == 0 ? s.left : s.right;
                auto direction = side == 0 ? s.leftDirection : -s.leftDirection;
                for (uint32_t k = 0; k < b.count; ++k) {
                    const auto& p = r.points[b.first + k];
                    auto position = s.centre + direction * p.distance;
                    points << i << ',' << (side == 0 ? "left" : "right") << ',' << p.distance << ',' << p.ground << '\n';
                    lines << "v " << position.x << ' ' << p.ground << ' ' << position.y << '\n';
                }
                if (b.count > 1) {
                    lines << 'l';
                    for (uint32_t k = 0; k < b.count; ++k) lines << ' ' << vertex + k;
                    lines << '\n';
                }
                vertex += b.count;
            }
        }
        sections.close(); points.close(); lines.close();
        auto controls = open("-stream-controls.csv");
        controls << "from,to,dry_from,dry_to,kind\n";
        for (const auto& c : r.controls)
            controls << c.from << ',' << c.to << ',' << c.dryFrom << ',' << c.dryTo << ','
                     << (c.dryFrom && c.dryTo ? "dry-span" : "point-pinch") << '\n';
        controls.close();
        metadata << "section_version=" << StreamSections::kVersion
                 << "\nsection_policy=first bank on exact terrain triangles; full apron; no width clipping or terrain changes"
                 << "\nspill_control_policy=zero-depth points/spans are dry geometric controls, not wet connections"
                 << "\nsection_search_distance=" << build.settings.streamSections->searchDistance
                 << "\nsections=" << r.sections.size() << "\nbounded_sections=" << r.boundedSections
                 << "\ndry_sections=" << r.drySections << "\ndomain_limited_sections=" << r.domainLimitedSections
                 << "\nsearch_limited_sections=" << r.searchLimitedSections << "\nspill_controls=" << r.controls.size()
                 << "\nsections_ms=" << r.elapsedMs << "\nsections_bytes=" << r.payloadBytes()
                 << "\nsection_integrals=area and wetted ground length; lower bounds when a bank search is truncated"
                 << "\nsection_scope=three stations per edge; no continuous footprint, confluence mesh or stream queries\n";
    }
    if (build.channelCarving) {
        const auto& r = *build.channelCarving;
        const auto& b = r.budget;
        const auto& f = *build.generationFields;
        auto cuts = open("-channel-cuts.csv");
        cuts << std::setprecision(17) << "index,cut_depth,protected,ground,soil,bedrock\n";
        for (size_t i = 0; i < r.cutDepth.size(); ++i)
            cuts << i << ',' << r.cutDepth[i] << ',' << int(r.protectedCells[i]) << ',' << f.heightmap.heights[i] << ','
                 << f.soil[i] << ',' << f.bedrock[i] << '\n';
        cuts.close();
        metadata << std::setprecision(17) << "channel_carving_version=" << ChannelCarving::kVersion
                 << "\nchannel_policy=one soil-first cut pass below old ground; parabolic sections; descending bed; protected basin triangles"
                 << "\nchannel_hydrology=drainage, lake water, streams and sections rebuilt after carving; spill connection remains unresolved"
                 << "\nchannel_maximum_cut=" << build.settings.channelCarving->maximumCut
                 << "\nchannel_changed_cells=" << r.changedCells
                 << "\nchannel_initial_soil=" << b.initialSoil << "\nchannel_final_soil=" << b.finalSoil
                 << "\nchannel_exported_soil=" << b.exportedSoil << "\nchannel_exported_bedrock=" << b.exportedBedrock
                 << "\nchannel_removed_ground=" << b.removedGround << "\nchannel_material_residual=" << b.materialResidual
                 << "\nchannel_surface_rounding_delta=" << b.surfaceRoundingDelta
                 << "\nchannel_preparation_ms=" << build.statistics.channelPreparationMs
                 << "\nchannel_carving_ms=" << r.elapsedMs << "\nchannel_carving_bytes=" << r.payloadBytes()
                 << "\nchannel_cut_pgm=full domain, zero to channel_maximum_cut; protected mask white means unchanged basin/rim"
                 << "\nchannel_material_policy=removed soil and bedrock explicitly exported; no unaccounted fill"
                 << "\nerosion_budget_checkpoint=post-settlement BEFORE channel carving; channel budget starts at that final soil\n";
    }
    if (build.combinedWater) {
        const auto& r = *build.combinedWater;
        const auto& mesh = r.surface.mesh();
        auto obj = open("-combined-water.obj");
        auto vertices = open("-combined-water-vertices.csv");
        obj << std::setprecision(9) << "# Combined clipped stream/lake mesh; +Y up; playable crop\n";
        vertices << std::setprecision(9) << "vertex,x,y,z,depth,flow_x,flow_z,normal_x,normal_y,normal_z\n";
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            const auto& v = mesh.vertices[i];
            obj << "v " << v.position.x << ' ' << v.position.y << ' ' << v.position.z << '\n';
            vertices << i << ',' << v.position.x << ',' << v.position.y << ',' << v.position.z << ',' << v.depth << ','
                     << v.flow.x << ',' << v.flow.y << ',' << v.normal.x << ',' << v.normal.y << ',' << v.normal.z << '\n';
        }
        for (const auto& v : mesh.vertices) obj << "vn " << v.normal.x << ' ' << v.normal.y << ' ' << v.normal.z << '\n';
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            obj << 'f';
            for (size_t k = 0; k < 3; ++k) obj << ' ' << mesh.indices[i + k] + 1 << "//" << mesh.indices[i + k] + 1;
            obj << '\n';
        }
        obj.close(); vertices.close();
        auto samples = open("-combined-water-samples.csv");
        samples << std::setprecision(9) << "x,z,kind,lake,height,depth,flow_x,flow_z,shoreline_distance\n";
        for (int z = 0; z < hm.resolution; ++z) for (int x = 0; x < hm.resolution; ++x) {
            auto p = build.surface.position(x, z);
            auto sample = r.surface.sampleAt(p.x, p.z);
            auto shore = r.surface.shorelineDistanceAt(p.x, p.z);
            samples << p.x << ',' << p.z << ',';
            if (sample) samples << (sample->kind == TerrainWater::Kind::Lake ? "lake" : "stream") << ',' << sample->lake << ','
                                << sample->height << ',' << sample->depth << ',' << sample->flow.x << ',' << sample->flow.y << ',';
            else samples << "dry,-1,,0,0,0,";
            if (shore) samples << *shore;
            samples << '\n';
        }
        samples.close();
        metadata << std::setprecision(17) << "combined_water_version=" << TerrainWater::kVersion
                 << "\ncombined_water_policy=first downstream stream/lake receiver; projected stream levels; shared terrain triangles; seeded wet components"
                 << "\ncombined_water_join=stream/lake upper envelope; lake wins ties; stream mouths may cover lake shoreline triangles"
                 << "\ncombined_water_sinks=unsupplied basin vertices and zero-depth spills stay dry; no epsilon film"
                 << "\ncombined_water_flow=down the triangle water gradient; guide direction on flat streams; zero in flat lakes"
                 << "\ncombined_water_scope=static geometric reconstruction; no new flow/loss simulation or bank-width guarantee"
                 << "\ncombined_stream_triangles=" << r.streamTriangles << "\ncombined_lake_triangles=" << r.lakeTriangles
                 << "\ncombined_stream_area=" << r.streamArea << "\ncombined_lake_area=" << r.lakeArea
                 << "\ncombined_shore_segments=" << r.surface.shores().size()
                 << "\ncombined_shore_index_bytes=" << r.surface.shorelineIndexBytes()
                 << "\ncombined_water_ms=" << r.elapsedMs << "\ncombined_water_bytes=" << r.payloadBytes()
                 << "\ncombined_shoreline=full-apron zero-depth contours; no crop boundary bank; exact nearest-segment BVH\n";
    }
    if (build.playability) {
        const auto& r = *build.playability;
        const auto& s = *build.settings.playability;
        auto centre = [&](uint32_t cell) {
            int x = cell % r.resolution, z = cell / r.resolution;
            auto a = build.surface.position(x, z), b = build.surface.position(x + 1, z + 1);
            return glm::vec2(float((double(a.x) + b.x) * .5), float((double(a.z) + b.z) * .5));
        };
        auto cells = open("-playability.csv");
        cells << std::setprecision(9) << "cell,x,z,terrain_flags,footprint_flags,component\n";
        for (uint32_t cell = 0; cell < r.flags.size(); ++cell) {
            auto p = centre(cell);
            cells << cell << ',' << p.x << ',' << p.y << ',' << int(r.terrainFlags[cell]) << ',' << int(r.flags[cell]) << ',' << r.component[cell] << '\n';
        }
        cells.close();
        auto route = open("-route.csv");
        route << std::setprecision(9) << "step,cell,x,y,z\n";
        for (size_t k = 0; k < r.route.size(); ++k) {
            auto p = centre(r.route[k]);
            route << k << ',' << r.route[k] << ',' << p.x << ',' << build.surface.heightAt(p.x, p.y) << ',' << p.y << '\n';
        }
        route.close();
        auto components = open("-components.csv");
        components << std::setprecision(17) << "component,first_cell,cells,area\n";
        for (size_t k = 0; k < r.components.size(); ++k) {
            const auto& c = r.components[k];
            components << k << ',' << c.first << ',' << c.cells << ',' << c.area << '\n';
        }
        components.close();
        metadata << std::setprecision(17) << "playability_version=" << TerrainPlayability::kVersion
                 << "\nplayability_status=" << TerrainPlayability::statusName(r.status)
                 << "\nplayability_resolution=" << r.resolution << "\nplayability_components=" << r.components.size()
                 << "\nplayability_hull_width=" << s.hullWidth << "\nplayability_hull_length=" << s.hullLength
                 << "\nplayability_clearance=" << s.clearance << "\nplayability_footprint_radius=" << r.footprintRadius
                 << "\nplayability_boundary_half_extent=" << r.boundaryHalfExtent
                 << "\nplayability_maximum_slope=" << s.maximumSlopeDegrees << "\nplayability_spawn_slope=" << s.spawnSlopeDegrees
                 << "\nplayability_minimum_area=" << s.minimumConnectedArea << "\nplayability_minimum_span=" << s.minimumRouteSpan
                 << "\nplayability_route_length=" << r.routeLength << "\nplayability_route_span=" << r.routeSpan
                 << "\nplayability_ms=" << r.elapsedMs << "\nplayability_bytes=" << r.payloadBytes()
                 << "\nplayability_working_bytes=" << r.workingBytes
                 << "\nplayability_flags=1 water; 2 route slope; 4 spawn slope; 8 obstacle; 16 boundary"
                 << "\nplayability_policy=whole-cell rotational envelope; exact terrain faces; any wet triangle blocks its quad; four-neighbour routes"
                 << "\nplayability_selection=largest qualifying centre area; dry candidate nearest component centroid; farthest reachable goal"
                 << "\nplayability_scope=conservative static geometry; no placed scenery in generator; no dynamics or seed retries\n";
        if (r.spawn) metadata << "spawn_x=" << r.spawn->position.x << "\nspawn_y=" << r.spawn->position.y << "\nspawn_z=" << r.spawn->position.z
                              << "\nspawn_forward_x=" << r.spawn->forward.x << "\nspawn_forward_z=" << r.spawn->forward.y
                              << "\nspawn_component=" << r.spawn->component << "\nspawn_connected_area=" << r.components[r.spawn->component].area << '\n';
    }
    metadata.close();
}
}

int main(int argc, char** argv) {
    try {
        TerrainGenerator::Settings settings;
        std::optional<uint32_t> seed;
        int repeats = 3;
        std::filesystem::path directory;
        bool explicitResolution = false, macroOptions = false, erosionOptions = false;
        bool lakeWater = false, lakeOptions = false;
        LakeWater::Settings lakeSettings;
        bool streams = false, streamOptions = false;
        StreamNetwork::Settings streamSettings;
        bool streamSections = false, sectionOptions = false;
        StreamSections::Settings sectionSettings;
        bool channelCarving = false, carvingOptions = false;
        ChannelCarving::Settings carvingSettings;
        bool playability = false, playabilityOptions = false, failedPlayability = false;
        TerrainPlayability::Settings playabilitySettings;
        for (int i = 1; i < argc; ++i) {
            std::string_view option(argv[i]);
            if (option == "--help") {
                std::cout << "terrain_probe [--preset legacy|rolling-valley|eroded-valley|drained-valley] [--seed N] [--resolution N]\n"
                             "  [--world-size N] [--repeats N] [--output-dir PATH]\n"
                             "  Rolling valley: [--relief N] [--valley-width N] [--feature-scale N]\n"
                             "                  [--soil-depth N] [--apron-width N]\n"
                             "  Landforms: [--landform mixed|valley|hills|ridges|plain|basin] [--warp-strength N]\n"
                             "  Erosion: [--erosion-seconds N] [--rain-seconds N] [--max-timestep N] [--talus-passes N]\n"
                             "           [--erosion-workers 1..4] [--terrain-refinement off|on|2x|4x]\n"
                             "  Drained valley: [--lake-water on|off] [--lake-evaporation N] [--lake-seepage N]\n"
                             "  Stream guides (requires lakes): [--streams on|off] [--stream-min-discharge N]\n"
                             "  Bank surveys (requires streams): [--stream-sections on|off] [--bank-search-distance N]\n"
                             "  Terrain edits (requires streams): [--channel-carving on|off] [--channel-max-cut N]\n"
                             "  Combined water mesh/queries (requires streams): [--combined-water on|off]\n"
                             "  Spawn/routes (requires combined water): [--playability on|off]\n"
                             "    [--tank-width N] [--tank-length N] [--spawn-clearance N] [--play-boundary-inset N]\n"
                             "    [--route-max-slope N] [--spawn-max-slope N] [--play-min-area N] [--play-min-span N]\n"
                             "Defaults: legacy=256 / valley presets=257 samples, 180 units, 16 fixed seeds, 3 uncached builds.\n"
                             "CSV on stdout; median/slowest on stderr; optional mesh and field exports.\n"
                             "Exit 2 if requested playability analysis rejects any map; diagnostics still export.\n";
                return 0;
            }
            if (++i >= argc) throw std::invalid_argument("missing option value");
            if (option == "--seed") seed = number<uint32_t>(argv[i]);
            else if (option == "--preset") {
                std::string_view preset(argv[i]);
                if (preset == "legacy") settings.preset = TerrainGenerator::Preset::Legacy;
                else if (preset == "rolling-valley") settings.preset = TerrainGenerator::Preset::RollingValley;
                else if (preset == "eroded-valley") settings.preset = TerrainGenerator::Preset::ErodedValley;
                else if (preset == "drained-valley") settings.preset = TerrainGenerator::Preset::DrainedValley;
                else throw std::invalid_argument("unknown terrain preset");
            }
            else if (option == "--resolution") { settings.resolution = number<int>(argv[i]); explicitResolution = true; }
            else if (option == "--world-size") settings.worldSize = number<float>(argv[i]);
            else if (option == "--landform") { settings.macro.landform = MacroTerrain::parseLandform(argv[i]); macroOptions = true; }
            else if (option == "--warp-strength") { settings.macro.warpStrength = number<float>(argv[i]); macroOptions = true; }
            else if (option == "--relief") { settings.macro.relief = number<float>(argv[i]); macroOptions = true; }
            else if (option == "--valley-width") { settings.macro.valleyWidth = number<float>(argv[i]); macroOptions = true; }
            else if (option == "--feature-scale") { settings.macro.featureScale = number<float>(argv[i]); macroOptions = true; }
            else if (option == "--soil-depth") { settings.macro.soilDepth = number<float>(argv[i]); macroOptions = true; }
            else if (option == "--apron-width") { settings.macro.apronWidth = number<float>(argv[i]); macroOptions = true; }
            else if (option == "--terrain-refinement") {
                settings.refinementPasses = TerrainRefinement::parsePasses(argv[i]);
            }
            else if (option == "--erosion-seconds") { settings.erosion.duration = number<double>(argv[i]); erosionOptions = true; }
            else if (option == "--rain-seconds") { settings.erosion.rainDuration = number<double>(argv[i]); erosionOptions = true; }
            else if (option == "--max-timestep") { settings.erosion.maxTimestep = number<double>(argv[i]); erosionOptions = true; }
            else if (option == "--talus-passes") { settings.erosion.talusPasses = number<int>(argv[i]); erosionOptions = true; }
            else if (option == "--erosion-workers") { settings.erosion.workers = number<int>(argv[i]); erosionOptions = true; }
            else if (option == "--lake-water") {
                std::string_view value(argv[i]);
                if (value != "on" && value != "off") throw std::invalid_argument("--lake-water expects on or off");
                lakeWater = value == "on";
            }
            else if (option == "--lake-evaporation") { lakeSettings.evaporation = number<double>(argv[i]); lakeOptions = true; }
            else if (option == "--lake-seepage") { lakeSettings.seepage = number<double>(argv[i]); lakeOptions = true; }
            else if (option == "--streams") {
                std::string_view value(argv[i]);
                if (value != "on" && value != "off") throw std::invalid_argument("--streams expects on or off");
                streams = value == "on";
            }
            else if (option == "--stream-min-discharge") { streamSettings.minimumDischarge = number<double>(argv[i]); streamOptions = true; }
            else if (option == "--stream-sections") {
                std::string_view value(argv[i]);
                if (value != "on" && value != "off") throw std::invalid_argument("--stream-sections expects on or off");
                streamSections = value == "on";
            }
            else if (option == "--bank-search-distance") { sectionSettings.searchDistance = number<float>(argv[i]); sectionOptions = true; }
            else if (option == "--channel-carving") {
                std::string_view value(argv[i]);
                if (value != "on" && value != "off") throw std::invalid_argument("--channel-carving expects on or off");
                channelCarving = value == "on";
            }
            else if (option == "--channel-max-cut") { carvingSettings.maximumCut = number<float>(argv[i]); carvingOptions = true; }
            else if (option == "--combined-water") {
                std::string_view value(argv[i]);
                if (value != "on" && value != "off") throw std::invalid_argument("--combined-water expects on or off");
                settings.combinedWater = value == "on";
            }
            else if (option == "--repeats") repeats = number<int>(argv[i]);
            else if (option == "--playability") {
                std::string_view value(argv[i]);
                if (value != "on" && value != "off") throw std::invalid_argument("--playability expects on or off");
                playability = value == "on";
            }
            else if (option == "--tank-width") { playabilitySettings.hullWidth = number<float>(argv[i]); playabilityOptions = true; }
            else if (option == "--tank-length") { playabilitySettings.hullLength = number<float>(argv[i]); playabilityOptions = true; }
            else if (option == "--spawn-clearance") { playabilitySettings.clearance = number<float>(argv[i]); playabilityOptions = true; }
            else if (option == "--play-boundary-inset") { playabilitySettings.boundaryInsetFraction = number<float>(argv[i]); playabilityOptions = true; }
            else if (option == "--route-max-slope") { playabilitySettings.maximumSlopeDegrees = number<float>(argv[i]); playabilityOptions = true; }
            else if (option == "--spawn-max-slope") { playabilitySettings.spawnSlopeDegrees = number<float>(argv[i]); playabilityOptions = true; }
            else if (option == "--play-min-area") { playabilitySettings.minimumConnectedArea = number<double>(argv[i]); playabilityOptions = true; }
            else if (option == "--play-min-span") { playabilitySettings.minimumRouteSpan = number<double>(argv[i]); playabilityOptions = true; }
            else if (option == "--output-dir") directory = argv[i];
            else throw std::invalid_argument("unknown terrain_probe option");
        }
        if (settings.preset != TerrainGenerator::Preset::Legacy && !explicitResolution) settings.resolution = 257;
        if (macroOptions && settings.preset == TerrainGenerator::Preset::Legacy)
            throw std::invalid_argument("macro options require a valley preset");
        if (erosionOptions && settings.preset != TerrainGenerator::Preset::ErodedValley &&
            settings.preset != TerrainGenerator::Preset::DrainedValley)
            throw std::invalid_argument("erosion options require --preset eroded-valley or drained-valley");
        if (repeats < 1 || repeats > 100) throw std::invalid_argument("repeats must be 1..100");
        if (lakeOptions && !lakeWater) throw std::invalid_argument("lake loss options require --lake-water on");
        if (lakeWater) settings.lakes = lakeSettings;
        if (streamOptions && !streams) throw std::invalid_argument("stream options require --streams on");
        if (streams) settings.streams = streamSettings;
        if (sectionOptions && !streamSections) throw std::invalid_argument("bank search options require --stream-sections on");
        if (streamSections) settings.streamSections = sectionSettings;
        if (carvingOptions && !channelCarving) throw std::invalid_argument("channel cut options require --channel-carving on");
        if (channelCarving) settings.channelCarving = carvingSettings;
        if (playabilityOptions && !playability) throw std::invalid_argument("playability options require --playability on");
        if (playability) settings.playability = playabilitySettings;
        std::vector<uint32_t> seeds(TerrainGenerator::kRegressionSeeds.begin(), TerrainGenerator::kRegressionSeeds.end());
        if (seed) seeds = {*seed};
        std::vector<double> times;
        std::cout << "preset,version,seed,resolution,repeat,heightfield_ms,surface_ms,mesh_ms,total_ms,surface_bytes,mesh_bytes,field_bytes,mesh_fnv1a64,field_fnv1a64,erosion_ms,erosion_steps,erosion_field_bytes,erosion_working_bytes,water_residual,solid_residual,solid_rounding_delta,settlement_ms,drainage_ms,drainage_field_bytes,drainage_working_bytes,basins,runoff_residual,removed_transient_water,settled_sediment,lake_version,water_ms,water_bytes,lakes_present,water_triangles,lake_runoff_residual,stream_version,stream_ms,stream_bytes,stream_nodes,stream_reaches,stream_confluences,stream_deficient_nodes,stream_max_deficit,section_version,sections_ms,sections_bytes,sections,bounded_sections,dry_sections,domain_limited_sections,search_limited_sections,spill_controls,carving_version,channel_preparation_ms,carving_ms,carving_bytes,carved_cells,exported_soil,exported_bedrock,removed_ground,carving_residual,carving_rounding_delta,combined_water_version,combined_water_ms,combined_water_bytes,combined_stream_triangles,combined_lake_triangles,combined_stream_area,combined_lake_area,playability_version,playability_status,playability_ms,playability_bytes,playability_working_bytes,playability_components,spawn_x,spawn_y,spawn_z,spawn_connected_area,route_length,route_span,landform_version,landform_requested,landform_resolved,warp_strength,final_resolution,refinement_version,refinement_ms\n";
        for (uint32_t value : seeds) {
            settings.seed = value;
            std::optional<uint64_t> expected;
            std::optional<uint64_t> expectedFields;
            for (int repeat = 0; repeat < repeats; ++repeat) {
                auto build = TerrainGenerator::build(settings);
                failedPlayability |= build.playability && build.playability->status != TerrainPlayability::Status::Ready;
                uint64_t hash = fingerprint(build);
                uint64_t fieldsHash = fieldFingerprint(build);
                if (expected && *expected != hash) throw std::runtime_error("non-deterministic terrain build");
                expected = hash;
                if (expectedFields && *expectedFields != fieldsHash) throw std::runtime_error("non-deterministic generation fields");
                expectedFields = fieldsHash;
                const auto& stats = build.statistics;
                times.push_back(stats.totalMs);
                std::cout << TerrainGenerator::presetName(settings.preset) << ',' << std::fixed << std::setprecision(3) << settings.version << ',' << value << ','
                          << settings.resolution << ',' << repeat << ',' << stats.heightfieldMs << ','
                          << stats.surfaceMs << ',' << stats.meshMs << ',' << stats.totalMs << ','
                          << stats.retainedSurfaceBytes << ',' << stats.meshBytes << ',' << stats.generationFieldBytes << ','
                          << std::hex << hash << ',' << fieldsHash << std::dec << ',' << stats.erosionMs << ','
                          << (build.erosion ? build.erosion->steps : 0) << ',' << stats.erosionFieldBytes << ',' << stats.erosionWorkingBytes;
                auto budget = build.erosion ? build.erosion->budget : HydraulicErosion::Budget{};
                std::cout << std::scientific << std::setprecision(9) << ',' << budget.waterResidual << ','
                          << budget.solidResidual << ',' << budget.solidRoundingDelta << ','
                          << std::fixed << std::setprecision(3) << stats.settlementMs << ',' << stats.drainageMs << ','
                          << stats.drainageFieldBytes << ',' << stats.drainageWorkingBytes << ','
                          << (build.drainage ? build.drainage->basins.size() : 0) << ','
                          << std::scientific << std::setprecision(9)
                          << (build.drainage ? build.drainage->runoffResidual : 0) << ','
                          << budget.removedTransientWater << ',' << budget.settledSediment << ','
                          << (build.water ? LakeWater::kVersion : 0) << ',' << std::fixed << std::setprecision(3)
                          << stats.waterMs << ',' << stats.waterBytes << ','
                          << (build.water ? std::count_if(build.water->lakes.begin(), build.water->lakes.end(),
                                                         [](const auto& lake) { return lake.present; }) : 0) << ','
                          << (build.water ? build.water->surface.mesh().indices.size() / 3 : 0) << ','
                          << std::scientific << std::setprecision(9) << (build.water ? build.water->runoffResidual : 0) << ','
                          << (build.streams ? StreamNetwork::kVersion : 0) << ',' << std::fixed << std::setprecision(3)
                          << stats.streamsMs << ',' << stats.streamsBytes << ','
                          << (build.streams ? build.streams->nodes.size() : 0) << ','
                          << (build.streams ? build.streams->reaches.size() : 0) << ','
                          << (build.streams ? build.streams->confluences : 0) << ','
                          << (build.streams ? build.streams->deficientNodes : 0) << ','
                          << std::scientific << std::setprecision(9) << (build.streams ? build.streams->maximumDeficit : 0) << ','
                          << (build.streamSections ? StreamSections::kVersion : 0) << ',' << std::fixed << std::setprecision(3)
                          << stats.streamSectionsMs << ',' << stats.streamSectionsBytes << ','
                          << (build.streamSections ? build.streamSections->sections.size() : 0) << ','
                          << (build.streamSections ? build.streamSections->boundedSections : 0) << ','
                          << (build.streamSections ? build.streamSections->drySections : 0) << ','
                          << (build.streamSections ? build.streamSections->domainLimitedSections : 0) << ','
                          << (build.streamSections ? build.streamSections->searchLimitedSections : 0) << ','
                          << (build.streamSections ? build.streamSections->controls.size() : 0) << ','
                          << (build.channelCarving ? ChannelCarving::kVersion : 0) << ','
                          << stats.channelPreparationMs << ',' << stats.channelCarvingMs << ',' << stats.channelCarvingBytes << ','
                          << (build.channelCarving ? build.channelCarving->changedCells : 0);
                const auto cut = build.channelCarving ? build.channelCarving->budget : ChannelCarving::Budget{};
                std::cout << std::scientific << std::setprecision(9) << ',' << cut.exportedSoil << ',' << cut.exportedBedrock << ','
                          << cut.removedGround << ',' << cut.materialResidual << ',' << cut.surfaceRoundingDelta << ','
                          << (build.combinedWater ? TerrainWater::kVersion : 0) << ',' << std::fixed << std::setprecision(3)
                          << stats.combinedWaterMs << ',' << stats.combinedWaterBytes << ','
                          << (build.combinedWater ? build.combinedWater->streamTriangles : 0) << ','
                          << (build.combinedWater ? build.combinedWater->lakeTriangles : 0) << ',' << std::scientific << std::setprecision(9)
                          << (build.combinedWater ? build.combinedWater->streamArea : 0) << ','
                          << (build.combinedWater ? build.combinedWater->lakeArea : 0) << ','
                          << (build.playability ? TerrainPlayability::kVersion : 0) << ','
                          << (build.playability ? TerrainPlayability::statusName(build.playability->status) : "not-requested") << ','
                          << std::fixed << std::setprecision(3) << stats.playabilityMs << ',' << stats.playabilityBytes << ','
                          << (build.playability ? build.playability->workingBytes : 0) << ','
                          << (build.playability ? build.playability->components.size() : 0) << ',';
                if (build.playability && build.playability->spawn) {
                    const auto& r = *build.playability;
                    std::cout << std::setprecision(9) << r.spawn->position.x << ',' << r.spawn->position.y << ',' << r.spawn->position.z << ','
                              << r.components[r.spawn->component].area << ',' << r.routeLength << ',' << r.routeSpan;
                } else std::cout << ",,,0,0,0";
                std::cout << ',' << MacroTerrain::kLandformVersion << ','
                          << MacroTerrain::landformName(settings.macro.landform) << ','
                          << MacroTerrain::landformName(MacroTerrain::resolveLandform(settings.macro.landform, value))
                          << ',' << settings.macro.warpStrength << ',' << build.surface.heightmap().resolution
                          << ',' << (build.refinement ? TerrainRefinement::kVersion : 0)
                          << ',' << (build.refinement ? build.refinement->elapsedMs : 0) << '\n';
                if (repeat == 0 && !directory.empty()) exportDiagnostics(build, directory);
            }
        }
        std::sort(times.begin(), times.end());
        double median = (times[(times.size() - 1) / 2] + times[times.size() / 2]) * .5;
        std::cerr << times.size() << " uncached CPU builds: median " << median << " ms, slowest "
                  << times.back() << " ms. Excludes exports, hashing, uploads, BLAS and scene startup.\n";
        if (failedPlayability) {
            std::cerr << "At least one map failed the requested playability criteria; see playability_status. No seed retry or fallback performed.\n";
            return 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "terrain_probe: " << e.what() << '\n';
        return 1;
    }
}
