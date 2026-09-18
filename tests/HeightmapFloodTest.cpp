#include "scene/HeightmapFlood.h"

#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid input accepted");
}
}

int main() {
    using namespace HeightmapFlood;
    constexpr int n = 5;
    HeightmapGenerator::Heightmap hm{n, 4.0f, std::vector<float>(size_t(n) * n, 10.0f)};
    // A 3x2 basin (i in [1,3], j in [1,2], 6 cells -- exactly kMinRegionCells)
    // sits at height 0; everything else stays at 10.
    for (int j = 1; j <= 2; ++j)
        for (int i = 1; i <= 3; ++i) hm.heights[size_t(j) * n + i] = 0.0f;
    // An isolated single-cell dip, disconnected from the basin above and too
    // small on its own (component size 1 < kMinRegionCells) to ever flood.
    hm.heights[size_t(4) * n + 4] = 0.0f;

    auto field = compute(hm, /*threshold=*/1.0f, /*maxDepth=*/5.0f);
    require(field.resolution == n && field.worldSize == 4.0f && field.maxDepth == 5.0f, "field metadata mismatch");
    for (int j = 1; j <= 2; ++j)
        for (int i = 1; i <= 3; ++i) {
            size_t idx = size_t(j) * n + i;
            // basinFloor(0) + maxDepth(5) exceeds threshold(1), so the level
            // is capped at the threshold that qualified the basin as low.
            require(field.submerged[idx] && field.waterLevel[idx] == 1.0f, "basin cell not submerged at capped level");
        }
    require(!field.submerged[size_t(0) * n + 0], "dry corner reported as submerged");
    require(!field.submerged[size_t(4) * n + 4], "single-cell dip below kMinRegionCells was flooded");

    TerrainSurface ground(hm);
    // Triangle 0 of quad (1,1): vertices (1,1),(1,2),(2,2) -- all three wet.
    require(triangleHasWater(field, ground, 2 * (1 * (n - 1) + 1)), "fully wet triangle reported dry");
    // Triangle 0 of quad (0,1): vertices (0,1) dry, (0,2) dry, (1,2) wet --
    // any single wet corner must still count as wet.
    require(triangleHasWater(field, ground, 2 * (1 * (n - 1) + 0)), "single wet corner missed");
    // Triangle 0 of quad (0,3), far from the basin: all three corners dry.
    require(!triangleHasWater(field, ground, 2 * (3 * (n - 1) + 0)), "fully dry triangle reported wet");

    HeightmapGenerator::Heightmap mismatched{4, 3.0f, std::vector<float>(16, 1.0f)};
    TerrainSurface otherGround(mismatched);
    rejects([&] { triangleHasWater(field, otherGround, 0); });
}
