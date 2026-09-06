#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "Vertex.h"

namespace VoxelSurface {

struct CellHash {
    size_t operator()(const glm::ivec3& v) const {
        // Coordinate ranges exceed 31 cells even for small leaf sprays.
        // A polynomial with base 31 aliases whole diagonals of the crown.
        uint32_t h = static_cast<uint32_t>(v.x) * 73856093u
                   ^ static_cast<uint32_t>(v.y) * 19349663u
                   ^ static_cast<uint32_t>(v.z) * 83492791u;
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        return h;
    }
};
using Cells = std::unordered_set<glm::ivec3, CellHash>;

// Extract a rounded, smooth-shaded boundary from solid voxel samples.
// Geometry is baked once and shared by rasterization and ray tracing.
void appendMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                const Cells& cells, float voxelSize, glm::vec3 color, float uvScale);

}  // namespace VoxelSurface
