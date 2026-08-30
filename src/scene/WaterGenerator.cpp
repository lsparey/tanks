#include "WaterGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/glm.hpp>

#include "../render/Vertex.h"
#include "HeightmapGenerator.h"
#include "Terrain.h"

namespace {

// Matches Terrain's own grass tiling scale -- kept sensible for when water
// gets its own tiled texture later, though it's not visually load-bearing
// yet (water currently just uses the shared plain white texture, tinted by
// vertex color).
constexpr float kTextureRepeatsPerUnit = 1.0f / 3.0f;

// Ignore single-cell/tiny noise dips -- only real basins become ponds.
constexpr int kMinRegionCells = 6;

// A polygon vertex mid-clip: xz position plus the *terrain* height there
// (not the water level -- the clip test and edge interpolation both need
// the real terrain height; the final mesh vertex's Y is always the flat
// water level regardless, see buildMesh).
struct ClipVertex {
    glm::vec2 xz;
    float terrainHeight;
};

// Sutherland-Hodgman clip of a single triangle against the half-space
// terrainHeight <= level. Triangle in, and out, are CCW-wound (matching
// Terrain::buildMesh's convention) since clipping a convex polygon against
// one linear inequality preserves winding; the result has 0 vertices (fully
// dry), 3 (fully submerged, or a single corner clipped off), or 4 (a quad,
// when exactly one corner is dry) -- this is what gives the shoreline a
// smooth cut through each triangle instead of an all-or-nothing per-cell
// test, matching exactly where the actual (piecewise-linear) terrain
// surface crosses the water level.
std::vector<ClipVertex> clipTriangleBelowLevel(const ClipVertex& a, const ClipVertex& b,
                                                const ClipVertex& c, float level) {
    std::array<ClipVertex, 3> verts = {a, b, c};
    std::vector<ClipVertex> output;
    for (int i = 0; i < 3; ++i) {
        const ClipVertex& cur = verts[i];
        const ClipVertex& nxt = verts[(i + 1) % 3];
        bool curIn = cur.terrainHeight <= level;
        bool nxtIn = nxt.terrainHeight <= level;
        if (curIn) output.push_back(cur);
        if (curIn != nxtIn) {
            float t = (level - cur.terrainHeight) / (nxt.terrainHeight - cur.terrainHeight);
            output.push_back({glm::mix(cur.xz, nxt.xz, t), level});
        }
    }
    return output;
}

}  // namespace

WaterGenerator::FloodField WaterGenerator::computeFloodField(const Terrain& terrain, float threshold,
                                                               float maxDepth) {
    const HeightmapGenerator::Heightmap& hm = terrain.heightmap();
    int n = hm.resolution;

    FloodField field;
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

std::unique_ptr<Mesh> WaterGenerator::buildMesh(VulkanContext& ctx, CommandContext& commands,
                                                 const Terrain& terrain, const FloodField& field) {
    const HeightmapGenerator::Heightmap& hm = terrain.heightmap();
    int n = field.resolution;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // A natural pond/lake reads as a muted green-teal near the shore
    // (sediment/algae, light bouncing off the shallow bed) fading to a
    // desaturated blue-grey in deep water, rather than a bright saturated
    // teal-to-navy gradient.
    const glm::vec3 shallowColor(0.24f, 0.38f, 0.34f);
    const glm::vec3 deepColor(0.05f, 0.11f, 0.16f);

    auto worldXZ = [&](int i, int j) {
        float x = (static_cast<float>(i) / (n - 1) - 0.5f) * hm.worldSize;
        float z = (static_cast<float>(j) / (n - 1) - 0.5f) * hm.worldSize;
        return glm::vec2(x, z);
    };

    // The water level to clip a cell's two triangles against -- any
    // submerged corner's level, since adjacent submerged cells are always
    // the same connected basin (see computeFloodField) and therefore share
    // the same level. A cell with no submerged corner at all has no water
    // nearby and is skipped entirely.
    auto cellLevel = [&](int i, int j) -> const float* {
        int corners[4] = {j * n + i, j * n + i + 1, (j + 1) * n + i, (j + 1) * n + i + 1};
        for (int c : corners) {
            if (field.submerged[c]) return &field.waterLevel[c];
        }
        return nullptr;
    };

    auto emit = [&](const ClipVertex& v, float level) {
        float depth = level - v.terrainHeight;
        float depthT = glm::clamp(depth / field.maxDepth, 0.0f, 1.0f);
        glm::vec3 color = glm::mix(shallowColor, deepColor, depthT);
        glm::vec2 uv = v.xz * kTextureRepeatsPerUnit;
        vertices.push_back({glm::vec3(v.xz.x, level, v.xz.y), glm::vec3(0.0f, 1.0f, 0.0f), color, uv});
    };

    // Clip one triangle (CCW-wound) against `level` and fan-triangulate
    // whatever 0/3/4-vertex polygon comes out.
    auto processTriangle = [&](const ClipVertex& a, const ClipVertex& b, const ClipVertex& c,
                                float level) {
        std::vector<ClipVertex> poly = clipTriangleBelowLevel(a, b, c, level);
        if (poly.size() < 3) return;
        uint32_t base = static_cast<uint32_t>(vertices.size());
        for (const auto& v : poly) emit(v, level);
        for (size_t k = 1; k + 1 < poly.size(); ++k) {
            indices.push_back(base);
            indices.push_back(base + static_cast<uint32_t>(k));
            indices.push_back(base + static_cast<uint32_t>(k + 1));
        }
    };

    // Same diagonal split as Terrain::buildMesh: (00,01,11) and (00,11,10).
    for (int j = 0; j < n - 1; ++j) {
        for (int i = 0; i < n - 1; ++i) {
            const float* level = cellLevel(i, j);
            if (!level) continue;

            glm::vec2 p00 = worldXZ(i, j), p10 = worldXZ(i + 1, j);
            glm::vec2 p01 = worldXZ(i, j + 1), p11 = worldXZ(i + 1, j + 1);
            ClipVertex v00{p00, hm.at(i, j)}, v10{p10, hm.at(i + 1, j)};
            ClipVertex v01{p01, hm.at(i, j + 1)}, v11{p11, hm.at(i + 1, j + 1)};

            processTriangle(v00, v01, v11, *level);
            processTriangle(v00, v11, v10, *level);
        }
    }

    if (indices.empty()) return nullptr;

    return std::make_unique<Mesh>(ctx, commands, vertices, indices);
}

bool WaterGenerator::isUnderwater(const FloodField& field, float worldX, float worldZ) {
    if (field.resolution == 0) return false;
    float gx = (worldX / field.worldSize + 0.5f) * static_cast<float>(field.resolution - 1);
    float gz = (worldZ / field.worldSize + 0.5f) * static_cast<float>(field.resolution - 1);
    int i = std::clamp(static_cast<int>(std::lround(gx)), 0, field.resolution - 1);
    int j = std::clamp(static_cast<int>(std::lround(gz)), 0, field.resolution - 1);
    return field.submerged[static_cast<size_t>(j) * field.resolution + i];
}
