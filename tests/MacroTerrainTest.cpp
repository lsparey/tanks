#include "scene/TerrainGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void near(float a, float b, const char* message) {
    require(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < 3e-5f, message);
}
template<class F> void rejects(F&& f) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid macro settings were accepted");
}
void sameFields(const MacroTerrain::Fields& a, const MacroTerrain::Fields& b) {
    require(a.heightmap.heights == b.heightmap.heights && a.bedrock == b.bedrock &&
            a.soil == b.soil && a.erodibility == b.erodibility && a.openFaces == b.openFaces,
            "non-deterministic macro fields");
}
}

int main() {
    TerrainGenerator::Settings settings;
    settings.preset = TerrainGenerator::Preset::RollingValley;
    settings.resolution = 257;
    std::array<bool, 5> families{};
    for (auto requested : {MacroTerrain::Landform::Valley, MacroTerrain::Landform::Mixed,
                           MacroTerrain::Landform::Hills, MacroTerrain::Landform::Ridges,
                           MacroTerrain::Landform::Plain, MacroTerrain::Landform::Basin}) {
        settings.macro.landform = requested;
        std::vector<float> previous;
        for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
            settings.seed = seed;
            auto family = MacroTerrain::resolveLandform(requested, seed);
            if (requested == MacroTerrain::Landform::Mixed) families[size_t(family)] = true;
            auto build = TerrainGenerator::build(settings);
            require(build.generationFields.has_value(), "missing macro generation domain");
            const auto& fields = *build.generationFields;
            auto repeat = MacroTerrain::generate(settings.resolution, settings.worldSize, seed, settings.macro);
            sameFields(fields, repeat);
            require(fields.heightmap.heights != previous, "seed did not change macro terrain");
            previous = fields.heightmap.heights;
            int n = fields.heightmap.resolution;
            size_t count = size_t(n) * n;
            require(n == settings.resolution + fields.apronCells * 2, "incorrect domain dimensions");
            require(fields.apronCells * fields.spacing >= settings.macro.apronWidth &&
                    (fields.apronCells - 1) * fields.spacing < settings.macro.apronWidth,
                    "apron width must round up by less than one interval");
            require(fields.heightmap.heights.size() == count && fields.bedrock.size() == count &&
                    fields.soil.size() == count && fields.erodibility.size() == count &&
                    fields.openFaces.size() == count, "field dimensions disagree");
            size_t faceCount = 0;
            for (int z = 0; z < n; ++z) {
                for (int x = 0; x < n; ++x) {
                    size_t i = size_t(z) * n + x;
                    require(std::isfinite(fields.bedrock[i]) && std::isfinite(fields.soil[i]) &&
                            fields.soil[i] >= 0, "invalid rock/soil field");
                    require(std::isfinite(fields.erodibility[i]) && fields.erodibility[i] >= 0 &&
                            fields.erodibility[i] <= 1, "unbounded erodibility");
                    require(fields.heightmap.heights[i] == fields.bedrock[i] + fields.soil[i],
                            "height disagrees with material columns");
                    uint8_t expected = (x == 0 ? MacroTerrain::NegativeX : 0) |
                        (x == n - 1 ? MacroTerrain::PositiveX : 0) |
                        (z == 0 ? MacroTerrain::NegativeZ : 0) |
                        (z == n - 1 ? MacroTerrain::PositiveZ : 0);
                    require(fields.openFaces[i] == expected, "missing boundary outlet or interior leak");
                    for (int bit = 0; bit < 4; ++bit) faceCount += (fields.openFaces[i] >> bit) & 1;
                }
            }
            require(faceCount == size_t(n) * 4, "wrong open face count (including corners)");
            require(build.surface.heightmap().heights == fields.crop().heights, "render surface is not exact crop");
            require(build.statistics.generationFieldBytes == fields.payloadBytes(), "missing field memory accounting");
            size_t gentle = 0;
            for (const auto& vertex : build.mesh.vertices) {
                near(build.surface.heightAt(vertex.position.x, vertex.position.z), vertex.position.y,
                     "macro vertex contact mismatch");
                require(vertex.normal.y > 0 && std::isfinite(vertex.normal.y), "invalid macro normal");
                float slope = std::sqrt(vertex.normal.x * vertex.normal.x + vertex.normal.z * vertex.normal.z) /
                              vertex.normal.y;
                if (slope >= 1.5f)
                    throw std::runtime_error("macro slope " + std::to_string(slope) + " for seed " +
                                             std::to_string(seed) + " / " + MacroTerrain::landformName(family));
                gentle += slope < .45f;
            }
            // A composition guard, NOT a tank-footprint, obstacle or spawn test.
            require(gentle > build.mesh.vertices.size() * .6, "default macro landscape is mostly steep");

            auto widerSettings = settings.macro;
            widerSettings.apronWidth *= 2;
            auto wider = MacroTerrain::generate(257, 180, seed, widerSettings);
            int offset = wider.apronCells - fields.apronCells;
            for (int z = 0; z < n; ++z) {
                for (int x = 0; x < n; ++x) {
                    size_t a = size_t(z) * n + x;
                    size_t b = size_t(z + offset) * wider.heightmap.resolution + x + offset;
                    require(fields.heightmap.heights[a] == wider.heightmap.heights[b] &&
                            fields.bedrock[a] == wider.bedrock[b] && fields.soil[a] == wider.soil[b] &&
                            fields.erodibility[a] == wider.erodibility[b], "apron extension changed existing ground");
                }
            }

            auto finer = MacroTerrain::generate(513, 180, seed, settings.macro);
            for (int z = 0; z < 257; ++z) {
                for (int x = 0; x < 257; ++x) {
                    size_t a = size_t(z + fields.apronCells) * n + x + fields.apronCells;
                    size_t b = size_t(2 * z + finer.apronCells) * finer.heightmap.resolution + 2 * x + finer.apronCells;
                    near(fields.heightmap.heights[a], finer.heightmap.heights[b], "resolution changed landform scale");
                    near(fields.soil[a], finer.soil[b], "resolution changed soil depth");
                    near(fields.erodibility[a], finer.erodibility[b], "resolution changed geology");
                }
            }
        }
    }
    require(std::all_of(families.begin(), families.end(), [](bool covered) { return covered; }),
            "mixed seed sweep did not exercise every landform family");
    MacroTerrain::Settings bare;
    bare.soilDepth = 0;
    auto rock = MacroTerrain::generate(33, 180, 7331, bare);
    require(std::all_of(rock.soil.begin(), rock.soil.end(), [](float h) { return h == 0; }), "bare preset has soil");
    bare.relief = 0;
    auto flat = MacroTerrain::generate(33, 180, 7331, bare);
    require(std::all_of(flat.heightmap.heights.begin(), flat.heightmap.heights.end(),
                        [](float h) { return h == 0; }), "zero relief is not flat");
    for (int invalid = 0; invalid < 11; ++invalid) {
        MacroTerrain::Settings bad;
        if (invalid == 0) bad.apronWidth = 0;
        if (invalid == 1) bad.apronWidth = 100;
        if (invalid == 2) bad.soilDepth = -1;
        if (invalid == 3) bad.relief = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 4) bad.valleyWidth = 0;
        if (invalid == 5) bad.featureScale = std::numeric_limits<float>::infinity();
        if (invalid == 7) bad.landform = static_cast<MacroTerrain::Landform>(999);
        if (invalid == 8) bad.warpStrength = -1;
        if (invalid == 9) bad.warpStrength = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 10) {
            bad.landform = MacroTerrain::Landform::Hills;
            bad.warpStrength = bad.featureScale * 2 + 1;
        }
        rejects([&] { MacroTerrain::generate(invalid == 6 ? 4097 : 257, 180, 7331, bad); });
    }
    for (auto form : {MacroTerrain::Landform::Hills, MacroTerrain::Landform::Ridges,
                      MacroTerrain::Landform::Plain, MacroTerrain::Landform::Basin}) {
        MacroTerrain::Settings varied;
        varied.landform = form;
        require(MacroTerrain::parseLandform(MacroTerrain::landformName(form)) == form, "landform name round trip");
        auto warped = MacroTerrain::generate(65, 180, 42, varied);
        varied.warpStrength = 0;
        auto unwarped = MacroTerrain::generate(65, 180, 42, varied);
        require(warped.heightmap.heights != unwarped.heightmap.heights, "warp control does not change the field");
        varied.relief = 0;
        varied.soilDepth = 0;
        auto flatNew = MacroTerrain::generate(33, 180, 42, varied);
        require(std::all_of(flatNew.heightmap.heights.begin(), flatNew.heightmap.heights.end(),
                           [](float h) { return h == 0; }), "zero relief is not flat for new family");
    }
    rejects([] { MacroTerrain::parseLandform("desert"); });
    // The valley comparison ignores the new warp control, so its historical
    // feature-scale range must remain valid with default settings.
    MacroTerrain::Settings smallScale;
    smallScale.featureScale = 1;
    MacroTerrain::generate(9, 180, 42, smallScale);
    settings.preset = static_cast<TerrainGenerator::Preset>(999);
    rejects([&] { TerrainGenerator::build(settings); });
    auto legacy = TerrainGenerator::build({});
    require(!legacy.generationFields && legacy.statistics.generationFieldBytes == 0, "legacy allocated macro fields");
}
