#pragma once

#include <array>
#include <glm/glm.hpp>

#include "HeightmapGenerator.h"

// Immutable CPU surface. Heights and shading normals live at grid vertices,
// row-major (z * resolution + x). X/Z and height use world units, +Y is up.
// The finite square is centred at the origin; outside queries clamp to its edge.
class TerrainSurface {
public:
    explicit TerrainSurface(HeightmapGenerator::Heightmap heightmap);

    const HeightmapGenerator::Heightmap& heightmap() const { return heightmap_; }
    const std::vector<glm::vec3>& shadingNormals() const { return shadingNormals_; }
    glm::vec3 position(int x, int z) const;

    // Each quad uses (00,01,11), (00,11,10), with upward winding.
    // The diagonal belongs to the first triangle; interior grid lines belong
    // to the cell on their positive side (the outer edge uses the last cell).
    static std::array<uint32_t, 6> quadIndices(int resolution, int x, int z);
    float heightAt(float worldX, float worldZ) const;
    glm::vec3 contactNormalAt(float worldX, float worldZ) const;
    glm::vec3 shadingNormalAt(float worldX, float worldZ) const;
    // Shared triangle selection for fields sampled against this exact mesh.
    struct Sample {
        std::array<uint32_t, 3> indices;
        glm::vec3 weights;
        uint32_t triangle; // row-major quads, two triangles per quad
    };
    Sample sampleAt(float worldX, float worldZ) const;

private:
    HeightmapGenerator::Heightmap heightmap_;
    std::vector<glm::vec3> shadingNormals_;
};
