#include "MacroTerrain.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <glm/glm.hpp>

namespace MacroTerrain {
namespace {
uint32_t hash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
float unit(uint32_t value) { return float(hash(value) >> 8) * (1.0f / 16777216.0f); }

// Seeded quintic value noise. Frequencies are in world units; neither sample
// count nor apron width enters the field recipe. No periodic hill waves.
float noise(float x, float z, uint32_t seed) {
    int ix = int(std::floor(x)), iz = int(std::floor(z));
    auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    float tx = fade(x - ix), tz = fade(z - iz);
    auto lattice = [&](int i, int j) {
        return unit(uint32_t(i) * 0x9e3779b9u ^ uint32_t(j) * 0x85ebca6bu ^ seed);
    };
    return glm::mix(glm::mix(lattice(ix, iz), lattice(ix + 1, iz), tx),
                    glm::mix(lattice(ix, iz + 1), lattice(ix + 1, iz + 1), tx), tz);
}
bool inRange(float value, float minimum, float maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}
}

const char* landformName(Landform form) {
    switch (form) {
        case Landform::Valley: return "valley";
        case Landform::Hills: return "hills";
        case Landform::Ridges: return "ridges";
        case Landform::Plain: return "plain";
        case Landform::Basin: return "basin";
        case Landform::Mixed: return "mixed";
    }
    throw std::invalid_argument("invalid landform");
}

Landform parseLandform(std::string_view name) {
    for (auto form : {Landform::Valley, Landform::Hills, Landform::Ridges,
                      Landform::Plain, Landform::Basin, Landform::Mixed})
        if (name == landformName(form)) return form;
    throw std::invalid_argument("landform requires mixed, valley, hills, ridges, plain or basin");
}

Landform resolveLandform(Landform form, uint32_t seed) {
    landformName(form); // validate before converting/indexing
    constexpr Landform families[]{Landform::Valley, Landform::Hills, Landform::Ridges,
                                  Landform::Plain, Landform::Basin};
    return form == Landform::Mixed ? families[hash(seed ^ 0x7a143595u) % 5] : form;
}

HeightmapGenerator::Heightmap Fields::crop() const {
    HeightmapGenerator::Heightmap result{playableResolution, playableWorldSize,
        std::vector<float>(size_t(playableResolution) * playableResolution)};
    for (int z = 0; z < playableResolution; ++z) {
        auto from = heightmap.heights.begin() + size_t(z + apronCells) * heightmap.resolution + apronCells;
        std::copy_n(from, playableResolution, result.heights.begin() + size_t(z) * playableResolution);
    }
    return result;
}

size_t Fields::payloadBytes() const {
    return (heightmap.heights.capacity() + bedrock.capacity() + soil.capacity() +
            erodibility.capacity()) * sizeof(float) + openFaces.capacity() * sizeof(uint8_t);
}

Fields generate(int playableResolution, float worldSize, uint32_t seed, const Settings& settings) {
    Landform form = resolveLandform(settings.landform, seed);
    if (playableResolution < 2 || playableResolution > 4097 ||
        !inRange(worldSize, 1.0f, 1000000.0f) ||
        !inRange(settings.relief, 0, 1000) || !inRange(settings.valleyWidth, 1, 1000000) ||
        !inRange(settings.featureScale, 1, 1000000) || !inRange(settings.soilDepth, 0, 100) ||
        !inRange(settings.apronWidth, 0.001f, worldSize * .5f) ||
        !inRange(settings.warpStrength, 0, 1000000) ||
        (settings.landform != Landform::Valley && settings.warpStrength > settings.featureScale * 2))
        throw std::invalid_argument("invalid macro terrain settings");
    Fields fields;
    fields.playableResolution = playableResolution;
    fields.playableWorldSize = worldSize;
    fields.spacing = worldSize / (playableResolution - 1);
    fields.apronCells = int(std::ceil(double(settings.apronWidth) / fields.spacing));
    int n = playableResolution + 2 * fields.apronCells;
    if (n > 4097) throw std::invalid_argument("rolling-valley domain including apron exceeds 4097 samples");
    fields.heightmap.resolution = n;
    fields.heightmap.worldSize = fields.spacing * (n - 1);
    size_t count = size_t(n) * n;
    fields.heightmap.heights.resize(count);
    fields.bedrock.resize(count);
    fields.soil.resize(count);
    fields.erodibility.resize(count);
    fields.openFaces.resize(count, 0);

    float angle = unit(seed ^ 0xa511e9b3u) * 6.28318530718f;
    glm::vec2 along(std::cos(angle), std::sin(angle));
    glm::vec2 across(-along.y, along.x);
    float scale = settings.featureScale;
    float grade = settings.relief / scale * glm::mix(.16f, .26f, unit(seed ^ 0x63d83595u));
    float valleyOffset = (unit(seed ^ 0xb5297a4du) - .5f) * settings.valleyWidth;
    float variedScale = scale * glm::mix(.85f, 1.2f, unit(seed ^ 0x259cab6du));
    float variedRelief = settings.relief * glm::mix(.8f, 1.2f, unit(seed ^ 0x59ba7841u));
    // Seeded phase prevents every field sharing a lattice corner at the origin.
    glm::vec2 phase(unit(seed ^ 0x75632ca1u) * 8, unit(seed ^ 0x942351bdu) * 8);
    glm::vec2 basinCentre((unit(seed ^ 0x893fad21u) - .5f) * scale * .7f,
                          (unit(seed ^ 0x184a965bu) - .5f) * scale * .7f);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            // Subtract integer apron offsets before converting to world space,
            // so extending the apron leaves every overlapping sample unchanged.
            glm::vec2 p((x - fields.apronCells - (playableResolution - 1) * .5f) * fields.spacing,
                        (z - fields.apronCells - (playableResolution - 1) * .5f) * fields.spacing);
            float u = glm::dot(p, along), v = glm::dot(p, across);
            float height, soilShelter;
            if (form == Landform::Valley) {
                float bend = (noise(u / (scale * 1.8f), .37f, seed ^ 0x68e31da4u) - .5f) * scale * .6f;
                float crossValley = (v - valleyOffset - bend) / settings.valleyWidth;
                // Rounded, connected valley and shoulders, with asymmetric broad
                // spurs. This is initial relief, not a stamped river or a water bed.
                float shoulder = std::sqrt(1 + crossValley * crossValley) - 1;
                float broad = noise(u / scale, v / scale, seed ^ 0x1b56c4e9u);
                float spur = noise(u / (scale * .65f) + 8.1f, v / (scale * 1.2f), seed ^ 0x9e3779b9u);
                float detail = noise(u / (scale * .38f), v / (scale * .38f), seed ^ 0xd1b54a35u);
                height = settings.relief * (shoulder * (.55f + .9f * spur) +
                                                   .6f * (broad - .5f) + .035f * (detail - .5f)) - grade * u;
                soilShelter = .3f + .7f / (1 + crossValley * crossValley);
            } else {
                glm::vec2 q = glm::vec2(u, v) / variedScale;
                glm::vec2 warp(
                    noise(q.x / 1.7f + phase.x, q.y / 1.7f + phase.y, seed ^ 0x641fd201u),
                    noise(q.x / 1.7f + phase.x, q.y / 1.7f + phase.y, seed ^ 0x925ca487u));
                glm::vec2 warped = glm::vec2(u, v) + (warp * 2.0f - 1.0f) * settings.warpStrength;
                q = warped / variedScale + phase;
                float hills = .65f * (noise(q.x, q.y, seed ^ 0x139fc4a7u) - .5f) +
                              .25f * (noise(q.x / .55f, q.y / .55f, seed ^ 0x7ab169d3u) - .5f) +
                              .10f * (noise(q.x / .28f, q.y / .28f, seed ^ 0x94a3625du) - .5f);
                // Rounded crests avoid the sharp fold of abs(noise), which
                // can become too steep when the domain warp compresses it.
                float ridgeNoise = 2 * noise(q.x / 1.6f, q.y / .8f, seed ^ 0x4681f2a3u) - 1;
                float ridge = 1 - ridgeNoise * ridgeNoise;
                glm::vec2 basin = (warped - basinCentre) / (variedScale * 1.3f);
                float bowl = std::sqrt(1 + glm::dot(basin, basin)) - 1;
                float shape = hills * 1.8f;
                if (form == Landform::Ridges) shape = .8f * ridge * ridge - .4f + .6f * hills;
                if (form == Landform::Plain) shape = .45f * hills;
                if (form == Landform::Basin) shape = .85f * bowl + .65f * hills;
                // Gentle regional tilt aids drainage without imposing a
                // single trough or requiring a river on every map.
                height = variedRelief * shape - grade * u * .35f;
                soilShelter = glm::clamp(.65f - shape * .25f, .3f, 1.0f);
            }
            // Coherent geology is independent of surface texture noise. The
            // valley starts with a thicker soil mantle; resistant shoulders
            // have less mobile material available to the future solver.
            float resistance = .2f + .65f * noise(p.x / (scale * .9f) + 3.7f,
                                                  p.y / (scale * .9f) - 5.2f, seed ^ 0x94d049bbu);
            float mantle = noise(p.x / (scale * .6f), p.y / (scale * .6f), seed ^ 0x369dea0fu);
            float soil = settings.soilDepth * (.6f + .8f * mantle) *
                         soilShelter * (1.15f - .5f * resistance);
            size_t i = size_t(z) * n + x;
            fields.bedrock[i] = height - soil;
            fields.soil[i] = soil;
            fields.erodibility[i] = 1 - resistance;
            fields.heightmap.heights[i] = fields.bedrock[i] + fields.soil[i];
            fields.openFaces[i] = (x == 0 ? NegativeX : 0) | (x == n - 1 ? PositiveX : 0) |
                                  (z == 0 ? NegativeZ : 0) | (z == n - 1 ? PositiveZ : 0);
        }
    }
    return fields;
}

} // namespace MacroTerrain
