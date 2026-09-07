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
    }
    return hash;
}

void exportDiagnostics(const TerrainGenerator::BuildResult& build, const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto& hm = build.surface.heightmap();
    std::string stem = std::string(TerrainGenerator::presetName(build.settings.preset)) + "-v" +
                       std::to_string(build.settings.version) + "-seed-" +
                       std::to_string(build.settings.seed) + "-" + std::to_string(hm.resolution);
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
    if (build.generationFields) {
        const auto& fields = *build.generationFields;
        auto fieldImage = [&](std::string_view suffix, const auto& values, double low, double high) {
            auto file = open(suffix);
            file << "P5\n" << fields.heightmap.resolution << ' ' << fields.heightmap.resolution << "\n65535\n";
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
        fieldImage("-bedrock.pgm", fields.bedrock, *rockMin, *rockMax);
        fieldImage("-soil.pgm", fields.soil, 0, soilMax);
        fieldImage("-erodibility.pgm", fields.erodibility, 0, 1);
        auto outlets = open("-outlets.pgm");
        outlets << "P5\n" << fields.heightmap.resolution << ' ' << fields.heightmap.resolution << "\n255\n";
        for (uint8_t value : fields.openFaces) outlets.put(static_cast<char>(value));
        outlets.close();
        metadata << "relief=" << build.settings.macro.relief << "\nvalley_width=" << build.settings.macro.valleyWidth
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
            struct Diagnostic { const char* name; const std::vector<double>* values; };
            for (const auto& diagnostic : {Diagnostic{"erosion", &e.erosion}, {"deposition", &e.deposition},
                                           {"water", &e.water}, {"sediment", &e.sediment},
                                           {"water-exposure", &e.waterExposure}, {"throughflow", &e.throughflow}}) {
                double maximum = *std::max_element(diagnostic.values->begin(), diagnostic.values->end());
                fieldImage(std::string("-") + diagnostic.name + ".pgm", *diagnostic.values, 0, maximum);
                metadata << diagnostic.name << "_max=" << maximum << '\n';
            }
            std::vector<double> change(e.water.size());
            double magnitude = 0;
            for (size_t i = 0; i < change.size(); ++i) {
                change[i] = e.deposition[i] - e.erosion[i] + e.relaxation[i];
                magnitude = std::max(magnitude, std::abs(change[i]));
            }
            fieldImage("-height-change.pgm", change, -magnitude, magnitude);
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
                     << "\nsolid_rounding_delta=" << b.solidRoundingDelta << '\n';
        }
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
        for (int i = 1; i < argc; ++i) {
            std::string_view option(argv[i]);
            if (option == "--help") {
                std::cout << "terrain_probe [--preset legacy|rolling-valley|eroded-valley] [--seed N] [--resolution N]\n"
                             "  [--world-size N] [--repeats N] [--output-dir PATH]\n"
                             "  Rolling valley: [--relief N] [--valley-width N] [--feature-scale N]\n"
                             "                  [--soil-depth N] [--apron-width N]\n"
                             "  Erosion: [--erosion-seconds N] [--rain-seconds N] [--max-timestep N] [--talus-passes N]\n"
                             "           [--erosion-workers 1..4]\n"
                             "Defaults: legacy=256 / valley presets=257 samples, 180 units, 16 fixed seeds, 3 uncached builds.\n"
                             "CSV on stdout; median/slowest on stderr; optional mesh and field exports.\n";
                return 0;
            }
            if (++i >= argc) throw std::invalid_argument("missing option value");
            if (option == "--seed") seed = number<uint32_t>(argv[i]);
            else if (option == "--preset") {
                std::string_view preset(argv[i]);
                if (preset == "legacy") settings.preset = TerrainGenerator::Preset::Legacy;
                else if (preset == "rolling-valley") settings.preset = TerrainGenerator::Preset::RollingValley;
                else if (preset == "eroded-valley") settings.preset = TerrainGenerator::Preset::ErodedValley;
                else throw std::invalid_argument("unknown terrain preset");
            }
            else if (option == "--resolution") { settings.resolution = number<int>(argv[i]); explicitResolution = true; }
            else if (option == "--world-size") settings.worldSize = number<float>(argv[i]);
            else if (option == "--relief") { settings.macro.relief = number<float>(argv[i]); macroOptions = true; }
            else if (option == "--valley-width") { settings.macro.valleyWidth = number<float>(argv[i]); macroOptions = true; }
            else if (option == "--feature-scale") { settings.macro.featureScale = number<float>(argv[i]); macroOptions = true; }
            else if (option == "--soil-depth") { settings.macro.soilDepth = number<float>(argv[i]); macroOptions = true; }
            else if (option == "--apron-width") { settings.macro.apronWidth = number<float>(argv[i]); macroOptions = true; }
            else if (option == "--erosion-seconds") { settings.erosion.duration = number<double>(argv[i]); erosionOptions = true; }
            else if (option == "--rain-seconds") { settings.erosion.rainDuration = number<double>(argv[i]); erosionOptions = true; }
            else if (option == "--max-timestep") { settings.erosion.maxTimestep = number<double>(argv[i]); erosionOptions = true; }
            else if (option == "--talus-passes") { settings.erosion.talusPasses = number<int>(argv[i]); erosionOptions = true; }
            else if (option == "--erosion-workers") { settings.erosion.workers = number<int>(argv[i]); erosionOptions = true; }
            else if (option == "--repeats") repeats = number<int>(argv[i]);
            else if (option == "--output-dir") directory = argv[i];
            else throw std::invalid_argument("unknown terrain_probe option");
        }
        if (settings.preset != TerrainGenerator::Preset::Legacy && !explicitResolution) settings.resolution = 257;
        if (macroOptions && settings.preset == TerrainGenerator::Preset::Legacy)
            throw std::invalid_argument("macro options require a valley preset");
        if (erosionOptions && settings.preset != TerrainGenerator::Preset::ErodedValley)
            throw std::invalid_argument("erosion options require --preset eroded-valley");
        if (repeats < 1 || repeats > 100) throw std::invalid_argument("repeats must be 1..100");
        std::vector<uint32_t> seeds(TerrainGenerator::kRegressionSeeds.begin(), TerrainGenerator::kRegressionSeeds.end());
        if (seed) seeds = {*seed};
        std::vector<double> times;
        std::cout << "preset,version,seed,resolution,repeat,heightfield_ms,surface_ms,mesh_ms,total_ms,surface_bytes,mesh_bytes,field_bytes,mesh_fnv1a64,field_fnv1a64,erosion_ms,erosion_steps,erosion_field_bytes,erosion_working_bytes,water_residual,solid_residual,solid_rounding_delta\n";
        for (uint32_t value : seeds) {
            settings.seed = value;
            std::optional<uint64_t> expected;
            std::optional<uint64_t> expectedFields;
            for (int repeat = 0; repeat < repeats; ++repeat) {
                auto build = TerrainGenerator::build(settings);
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
                          << budget.solidResidual << ',' << budget.solidRoundingDelta << '\n';
                if (repeat == 0 && !directory.empty()) exportDiagnostics(build, directory);
            }
        }
        std::sort(times.begin(), times.end());
        double median = (times[(times.size() - 1) / 2] + times[times.size() / 2]) * .5;
        std::cerr << times.size() << " uncached CPU builds: median " << median << " ms, slowest "
                  << times.back() << " ms. Excludes exports, hashing, uploads, BLAS and scene startup.\n";
    } catch (const std::exception& e) {
        std::cerr << "terrain_probe: " << e.what() << '\n';
        return 1;
    }
}
