#pragma once

#include <glm/glm.hpp>

#include "../render/AccelerationStructure.h"
#include "../render/CommandContext.h"
#include "../render/Mesh.h"
#include "../render/VulkanContext.h"
#include "TerrainGenerator.h"

// A generated heightmap mesh spanning [-worldSize/2, worldSize/2] in X and Z,
// with world-space height/normal sampling for terrain-following (used by
// Tank's ground clamping in M6).
class Terrain {
public:
    Terrain(VulkanContext& ctx, CommandContext& commands, TerrainGenerator::BuildResult build);

    void bindAndDraw(VkCommandBuffer cmd) const { mesh_.bindAndDraw(cmd); }

    float heightAt(float worldX, float worldZ) const { return surface_.heightAt(worldX, worldZ); }
    // Keep smooth normals for tank handling and broad placement/slope rules.
    glm::vec3 normalAt(float worldX, float worldZ) const { return surface_.shadingNormalAt(worldX, worldZ); }
    // Exact face normal for contact with the raster/BLAS triangle.
    glm::vec3 contactNormalAt(float worldX, float worldZ) const { return surface_.contactNormalAt(worldX, worldZ); }
    float worldSize() const { return surface_.heightmap().worldSize; }
    // Raw grid data, for anything that wants to analyze the terrain's shape
    // directly rather than sample it point-by-point (see WaterGenerator).
    const HeightmapGenerator::Heightmap& heightmap() const { return surface_.heightmap(); }

    // Terrain never changes after generation, so its BLAS is built once here
    // rather than managed externally.
    VkDeviceAddress blasAddress() const { return blas_.deviceAddress(); }

private:
    static Mesh uploadMesh(VulkanContext& ctx, CommandContext& commands,
                           const TerrainGenerator::MeshData& mesh);
    TerrainSurface surface_;
    Mesh mesh_;
    AccelerationStructure blas_;
};
