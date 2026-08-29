#pragma once

#include <glm/glm.hpp>

#include "../render/CommandContext.h"
#include "../render/Mesh.h"
#include "../render/VulkanContext.h"
#include "HeightmapGenerator.h"

// A generated heightmap mesh spanning [-worldSize/2, worldSize/2] in X and Z,
// with world-space height/normal sampling for terrain-following (used by
// Tank's ground clamping in M6).
class Terrain {
public:
    Terrain(VulkanContext& ctx, CommandContext& commands, int resolution, float worldSize,
            float amplitude);

    void bindAndDraw(VkCommandBuffer cmd) const { mesh_.bindAndDraw(cmd); }

    float heightAt(float worldX, float worldZ) const;
    glm::vec3 normalAt(float worldX, float worldZ) const;

private:
    static Mesh buildMesh(VulkanContext& ctx, CommandContext& commands,
                           const HeightmapGenerator::Heightmap& heightmap,
                           const std::vector<glm::vec3>& normals);
    static std::vector<glm::vec3> computeNormals(const HeightmapGenerator::Heightmap& heightmap);

    // Maps world (x,z) to fractional grid indices in [0, resolution-1].
    glm::vec2 worldToGrid(float worldX, float worldZ) const;

    HeightmapGenerator::Heightmap heightmap_;
    std::vector<glm::vec3> normals_;
    Mesh mesh_;
};
