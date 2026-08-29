#include "Terrain.h"

#include <algorithm>

namespace {
// White so the grass texture's own colors show through unmodified (the
// shader multiplies vertex color by the sampled texture).
constexpr glm::vec3 kTerrainColor(1.0f, 1.0f, 1.0f);
// Repeats the grass texture roughly every 3 world units across the terrain.
constexpr float kTextureRepeatsPerUnit = 1.0f / 3.0f;
}

Terrain::Terrain(VulkanContext& ctx, CommandContext& commands, int resolution, float worldSize,
                  float amplitude)
    : heightmap_(HeightmapGenerator::generateHills(resolution, worldSize, amplitude)),
      normals_(computeNormals(heightmap_)),
      mesh_(buildMesh(ctx, commands, heightmap_, normals_)) {}

std::vector<glm::vec3> Terrain::computeNormals(const HeightmapGenerator::Heightmap& hm) {
    std::vector<glm::vec3> normals(static_cast<size_t>(hm.resolution) * hm.resolution);
    float cellSize = hm.worldSize / (hm.resolution - 1);

    for (int j = 0; j < hm.resolution; ++j) {
        for (int i = 0; i < hm.resolution; ++i) {
            int iPrev = std::max(i - 1, 0);
            int iNext = std::min(i + 1, hm.resolution - 1);
            int jPrev = std::max(j - 1, 0);
            int jNext = std::min(j + 1, hm.resolution - 1);

            float dHdx = (hm.at(iNext, j) - hm.at(iPrev, j)) / ((iNext - iPrev) * cellSize);
            float dHdz = (hm.at(i, jNext) - hm.at(i, jPrev)) / ((jNext - jPrev) * cellSize);

            normals[j * hm.resolution + i] = glm::normalize(glm::vec3(-dHdx, 1.0f, -dHdz));
        }
    }
    return normals;
}

Mesh Terrain::buildMesh(VulkanContext& ctx, CommandContext& commands,
                         const HeightmapGenerator::Heightmap& hm,
                         const std::vector<glm::vec3>& normals) {
    std::vector<Vertex> vertices(static_cast<size_t>(hm.resolution) * hm.resolution);

    for (int j = 0; j < hm.resolution; ++j) {
        for (int i = 0; i < hm.resolution; ++i) {
            float x = (static_cast<float>(i) / (hm.resolution - 1) - 0.5f) * hm.worldSize;
            float z = (static_cast<float>(j) / (hm.resolution - 1) - 0.5f) * hm.worldSize;
            size_t idx = static_cast<size_t>(j) * hm.resolution + i;
            glm::vec2 uv(x * kTextureRepeatsPerUnit, z * kTextureRepeatsPerUnit);
            vertices[idx] = {glm::vec3(x, hm.heights[idx], z), normals[idx], kTerrainColor, uv};
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(hm.resolution - 1) * (hm.resolution - 1) * 6);

    // Winding matches the cube's +Y face convention (verified there via the
    // right-hand rule): for adjacent grid corners (i,j)/(i,j+1)/(i+1,j+1)/
    // (i+1,j), triangles (v00,v01,v11) and (v00,v11,v10) are CCW as seen
    // from above.
    auto indexOf = [&](int i, int j) { return static_cast<uint32_t>(j * hm.resolution + i); };
    for (int j = 0; j < hm.resolution - 1; ++j) {
        for (int i = 0; i < hm.resolution - 1; ++i) {
            uint32_t v00 = indexOf(i, j);
            uint32_t v01 = indexOf(i, j + 1);
            uint32_t v11 = indexOf(i + 1, j + 1);
            uint32_t v10 = indexOf(i + 1, j);
            indices.insert(indices.end(), {v00, v01, v11, v00, v11, v10});
        }
    }

    return Mesh(ctx, commands, vertices, indices);
}

glm::vec2 Terrain::worldToGrid(float worldX, float worldZ) const {
    float gx = (worldX / heightmap_.worldSize + 0.5f) * (heightmap_.resolution - 1);
    float gz = (worldZ / heightmap_.worldSize + 0.5f) * (heightmap_.resolution - 1);
    gx = std::clamp(gx, 0.0f, static_cast<float>(heightmap_.resolution - 1));
    gz = std::clamp(gz, 0.0f, static_cast<float>(heightmap_.resolution - 1));
    return {gx, gz};
}

float Terrain::heightAt(float worldX, float worldZ) const {
    glm::vec2 grid = worldToGrid(worldX, worldZ);
    int i0 = static_cast<int>(grid.x);
    int j0 = static_cast<int>(grid.y);
    int i1 = std::min(i0 + 1, heightmap_.resolution - 1);
    int j1 = std::min(j0 + 1, heightmap_.resolution - 1);
    float tx = grid.x - i0;
    float tz = grid.y - j0;

    float h0 = glm::mix(heightmap_.at(i0, j0), heightmap_.at(i1, j0), tx);
    float h1 = glm::mix(heightmap_.at(i0, j1), heightmap_.at(i1, j1), tx);
    return glm::mix(h0, h1, tz);
}

glm::vec3 Terrain::normalAt(float worldX, float worldZ) const {
    glm::vec2 grid = worldToGrid(worldX, worldZ);
    int i0 = static_cast<int>(grid.x);
    int j0 = static_cast<int>(grid.y);
    int i1 = std::min(i0 + 1, heightmap_.resolution - 1);
    int j1 = std::min(j0 + 1, heightmap_.resolution - 1);
    float tx = grid.x - i0;
    float tz = grid.y - j0;

    glm::vec3 n0 = glm::mix(normals_[j0 * heightmap_.resolution + i0],
                             normals_[j0 * heightmap_.resolution + i1], tx);
    glm::vec3 n1 = glm::mix(normals_[j1 * heightmap_.resolution + i0],
                             normals_[j1 * heightmap_.resolution + i1], tx);
    return glm::normalize(glm::mix(n0, n1, tz));
}
