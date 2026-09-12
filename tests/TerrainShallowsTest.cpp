#include "scene/TerrainGenerator.h"
#include "scene/TerrainShallows.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    MacroTerrain::Fields f;
    constexpr int n = 9;
    f.playableResolution = n; f.playableWorldSize = n - 1; f.spacing = 1;
    f.heightmap = {n, n - 1, std::vector<float>(n * n, 5)};
    for (int x = 0; x <= 6; ++x) f.heightmap.heights[4 * n + x] = x == 0 ? -2 : x < 4 ? 1 : -5;
    f.bedrock = f.heightmap.heights;
    f.soil.resize(n * n); f.erodibility.resize(n * n, 1); f.openFaces.resize(n * n);
    f.openFaces[4 * n] = MacroTerrain::NegativeX;
    auto d = TerrainDrainage::analyze(f);
    auto original = f;
    constexpr float maximumDepth = .6f, streamHead = .25f;
    auto fill = TerrainShallows::apply(f, d, maximumDepth - streamHead);
    require(fill.addedSoil > 0, "deep lake fixture did not receive fill");
    auto finalDrainage = TerrainDrainage::analyze(f);
    require(d.spillElevation == finalDrainage.spillElevation, "shallow beds changed lake rims or escape levels");
    for (size_t i = 0; i < f.soil.size(); ++i) {
        require(f.bedrock[i] == original.bedrock[i] && f.heightmap.heights[i] == f.bedrock[i] + f.soil[i],
                "shallow bed materials disagree with final ground");
        if (d.basin[i] < 0) require(f.heightmap.heights[i] == original.heightmap.heights[i], "dry terrain was raised");
    }
    require(TerrainShallows::apply(f, finalDrainage, maximumDepth - streamHead).addedSoil == 0,
            "repeating shallow bed shaping added more fill");
    auto lakes = LakeWater::build(f, finalDrainage);
    StreamNetwork::Settings s;
    s.minimumDischarge = .005; s.maximumDepth = streamHead;
    auto streams = StreamNetwork::build(f, finalDrainage, lakes, s);
    auto water = TerrainWater::build(f, finalDrainage, lakes, streams);
    require(!lakes.surface.mesh().vertices.empty() && water.streamTriangles, "depth fixture lost its lake or stream");
    auto standingWater = TerrainWater::build(f, finalDrainage, lakes, {});
    require(standingWater.lakeTriangles > 0, "standing lake fixture is empty");
    for (const auto& vertex : standingWater.surface.mesh().vertices)
        require(vertex.depth <= maximumDepth + 1e-5f, "standing lake exceeds maximum depth");
    const auto& mesh = water.surface.mesh();
    for (const auto& v : mesh.vertices)
        require(v.depth <= maximumDepth + 1e-5f, "rendered water exceeds maximum depth");
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        auto p = (mesh.vertices[mesh.indices[i]].position + mesh.vertices[mesh.indices[i + 1]].position +
                  mesh.vertices[mesh.indices[i + 2]].position) / 3.0f;
        auto sample = water.surface.sampleAt(p.x, p.z);
        require(sample && sample->depth <= maximumDepth + 1e-5f, "water query exceeds maximum depth");
    }
    bool rejected = false;
    try { TerrainShallows::apply(f, finalDrainage, 0); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "zero standing depth accepted");

    // Remove the physical trough, including negative-elevation soil, while
    // keeping adjacent dry ground unchanged. Width is independent of area.
    auto narrow = original;
    auto narrowLakes = LakeWater::build(narrow, d);
    auto removed = TerrainShallows::removeSmallLakes(narrow, d, narrowLakes, 1, 3);
    require(removed.removedLakes > 0 && removed.addedSoil > 0, "narrow lake survived width filter");
    auto dryDrainage = TerrainDrainage::analyze(narrow);
    auto dryLakes = LakeWater::build(narrow, dryDrainage);
    require(dryLakes.surface.mesh().vertices.empty(), "removed lake left rendered water");
    for (size_t i = 0; i < narrow.soil.size(); ++i) {
        require(narrow.heightmap.heights[i] == narrow.bedrock[i] + narrow.soil[i], "filled lake materials disagree");
        require(narrow.heightmap.heights[i] == (d.basin[i] >= 0 ? d.spillElevation[i] : original.heightmap.heights[i]),
                "small lake removal left a trench or changed dry ground");
    }

    MacroTerrain::Fields mixed;
    constexpr int side = 19;
    mixed.playableResolution = side; mixed.playableWorldSize = side - 1; mixed.spacing = 1;
    mixed.heightmap = {side, side - 1, std::vector<float>(side * side, 2)};
    for (int z = 4; z <= 15; ++z) for (int x = 4; x <= 15; ++x) mixed.heightmap.heights[z * side + x] = 0;
    mixed.heightmap.heights[side + 1] = 0; // isolated tiny puddle
    mixed.bedrock = mixed.heightmap.heights;
    mixed.soil.resize(side * side); mixed.erodibility.resize(side * side, 1); mixed.openFaces.resize(side * side);
    for (int z = 0; z < side; ++z) for (int x = 0; x < side; ++x)
        mixed.openFaces[z * side + x] = (x == 0 ? 1 : 0) | (x == side - 1 ? 2 : 0) |
                                         (z == 0 ? 4 : 0) | (z == side - 1 ? 8 : 0);
    auto md = TerrainDrainage::analyze(mixed);
    auto mw = LakeWater::build(mixed, md);
    auto filtered = TerrainShallows::removeSmallLakes(mixed, md, mw, 100, 3);
    require(filtered.removedLakes == 1, "lake size filter failed to distinguish pond from substantial lake");
    require(mixed.heightmap.heights[side + 1] == 2 && mixed.heightmap.heights[9 * side + 9] == 0,
            "size filter failed to fill pond or damaged large lake bed");
    auto rebuilt = TerrainDrainage::analyze(mixed);
    auto finalLakes = LakeWater::build(mixed, rebuilt);
    require(!finalLakes.surface.mesh().vertices.empty(), "size filter removed all substantial water");
    require(TerrainShallows::removeSmallLakes(mixed, rebuilt, finalLakes, 100, 3).removedLakes == 0,
            "small lake removal was not stable");
    auto invalid = mixed;
    invalid.heightmap.resolution = 0;
    rejected = false;
    try { TerrainShallows::removeSmallLakes(invalid, rebuilt, finalLakes, 100, 3); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && invalid.heightmap.heights == mixed.heightmap.heights,
            "invalid lake-removal domain was accepted or changed ground");
}
