#include "HeightmapFlood.h"

#include <algorithm>
#include <stdexcept>

namespace HeightmapFlood {
namespace {
// Ignore single-cell/tiny noise dips -- only real basins become ponds.
constexpr int kMinRegionCells = 6;
} // namespace

Field compute(const HeightmapGenerator::Heightmap& hm, float threshold, float maxDepth) {
    int n = hm.resolution;

    Field field;
    field.resolution = n;
    field.worldSize = hm.worldSize;
    field.maxDepth = maxDepth;
    field.submerged.assign(static_cast<size_t>(n) * n, false);
    field.waterLevel.assign(static_cast<size_t>(n) * n, 0.0f);

    std::vector<bool> visited(static_cast<size_t>(n) * n, false);
    std::vector<int> queue;

    // 4-connected flood fill over every cell with height < threshold. Each
    // connected component becomes one independent body of water: its water
    // level is its own basin floor (the lowest point within it) plus
    // maxDepth, capped at `threshold` so it never rises above the height
    // that qualified it as "low" in the first place. A cell only actually
    // ends up underwater if its own height is at or below that capped
    // level -- a basin can be low and connected overall while still having
    // shallower rim cells that a small maxDepth doesn't reach, and those
    // should stay dry rather than show a sunken/clipped water patch.
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            int idx = j * n + i;
            if (visited[idx] || hm.heights[idx] >= threshold) continue;

            std::vector<int> component;
            queue.clear();
            queue.push_back(idx);
            visited[idx] = true;
            size_t head = 0;
            while (head < queue.size()) {
                int cur = queue[head++];
                component.push_back(cur);
                int ci = cur % n;
                int cj = cur / n;
                const int dx[4] = {1, -1, 0, 0};
                const int dy[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; ++d) {
                    int ni = ci + dx[d];
                    int nj = cj + dy[d];
                    if (ni < 0 || ni >= n || nj < 0 || nj >= n) continue;
                    int nIdx = nj * n + ni;
                    if (visited[nIdx] || hm.heights[nIdx] >= threshold) continue;
                    visited[nIdx] = true;
                    queue.push_back(nIdx);
                }
            }

            if (static_cast<int>(component.size()) < kMinRegionCells) continue;

            float basinFloor = hm.heights[component[0]];
            for (int c : component) basinFloor = std::min(basinFloor, hm.heights[c]);
            float waterLevel = std::min(basinFloor + maxDepth, threshold);

            for (int c : component) {
                if (hm.heights[c] <= waterLevel) {
                    field.submerged[c] = true;
                    field.waterLevel[c] = waterLevel;
                }
            }
        }
    }
    return field;
}

bool triangleHasWater(const Field& field, const TerrainSurface& ground, uint32_t triangle) {
    int n = ground.heightmap().resolution;
    if (field.resolution != n) throw std::invalid_argument("flood field does not match ground resolution");
    uint32_t quad = triangle / 2;
    auto indices = TerrainSurface::quadIndices(n, int(quad % uint32_t(n - 1)), int(quad / uint32_t(n - 1)));
    for (int k = 0; k < 3; ++k)
        if (field.submerged[indices[(triangle % 2) * 3 + k]]) return true;
    return false;
}
} // namespace HeightmapFlood
