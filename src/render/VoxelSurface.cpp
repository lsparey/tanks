#include "VoxelSurface.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace VoxelSurface {
namespace {

struct SurfacePoint {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    // UVs use three planar projections. Share vertices within each plane,
    // while all copies keep exactly the same position and smooth normal.
    std::array<uint32_t, 3> output{UINT32_MAX, UINT32_MAX, UINT32_MAX};
};
struct Face {
    glm::ivec3 direction;
    std::array<glm::ivec3, 4> corners;
    int projection;
};
constexpr Face kFaces[] = {
    {{1, 0, 0}, {{{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}}, 0},
    {{-1, 0, 0}, {{{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}}}, 0},
    {{0, 1, 0}, {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}}, 1},
    {{0, -1, 0}, {{{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}}, 1},
    {{0, 0, 1}, {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}, 2},
    {{0, 0, -1}, {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}}, 2},
};
struct Quad {
    std::array<uint32_t, 4> points;
    int projection;
};

// Each boundary corner lies between eight voxel centres. Average their
// solid/empty edge crossings to round off the staircase, with displacement
// bounded by that one cell. This never samples procedural noise again.
glm::vec3 surfacePosition(const Cells& cells, glm::ivec3 corner, float voxelSize) {
    std::array<bool, 8> solid;
    for (int i = 0; i < 8; ++i)
        solid[i] = cells.contains(corner + glm::ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1) - 1);
    glm::vec3 crossings(0.0f);
    int count = 0;
    for (int axis = 0; axis < 3; ++axis) {
        int step = 1 << axis;
        for (int i = 0; i < 8; ++i) {
            if ((i & step) || solid[i] == solid[i + step]) continue;
            glm::vec3 p = glm::vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1) - 0.5f;
            p[axis] += 0.5f;
            crossings += p;
            ++count;
        }
    }
    return (glm::vec3(corner) + (count ? crossings / static_cast<float>(count) : glm::vec3(0))) * voxelSize;
}

}  // namespace

void appendMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                const Cells& cells, float voxelSize, glm::vec3 color, float uvScale) {
    std::unordered_map<glm::ivec3, uint32_t, CellHash> pointIds;
    std::vector<SurfacePoint> points;
    std::vector<Quad> quads;
    for (glm::ivec3 cell : cells) {
        for (const Face& face : kFaces) {
            if (cells.contains(cell + face.direction)) continue;
            Quad quad{{}, face.projection};
            for (int i = 0; i < 4; ++i) {
                glm::ivec3 corner = cell + face.corners[i];
                auto [it, inserted] = pointIds.try_emplace(corner, static_cast<uint32_t>(points.size()));
                if (inserted) points.push_back({surfacePosition(cells, corner, voxelSize)});
                quad.points[i] = it->second;
            }
            quads.push_back(quad);
        }
    }

    // Area-weighted normals on the rounded geometry, shared across UV seams.
    // Keep both triangles' contributions so non-planar quads shade correctly.
    constexpr int triangles[6] = {0, 1, 2, 0, 2, 3};
    for (const Quad& quad : quads) {
        for (int t = 0; t < 6; t += 3) {
            uint32_t a = quad.points[triangles[t]];
            uint32_t b = quad.points[triangles[t + 1]];
            uint32_t c = quad.points[triangles[t + 2]];
            glm::vec3 n = glm::cross(points[b].position - points[a].position,
                                    points[c].position - points[a].position);
            points[a].normal += n;
            points[b].normal += n;
            points[c].normal += n;
        }
    }
    vertices.reserve(vertices.size() + points.size() * 2);
    indices.reserve(indices.size() + quads.size() * 6);
    for (const Quad& quad : quads) {
        for (int corner : triangles) {
            SurfacePoint& point = points[quad.points[corner]];
            uint32_t& index = point.output[quad.projection];
            if (index == UINT32_MAX) {
                index = static_cast<uint32_t>(vertices.size());
                glm::vec3 p = point.position;
                glm::vec2 uv = quad.projection == 0 ? glm::vec2(p.y, p.z)
                             : quad.projection == 1 ? glm::vec2(p.x, p.z) : glm::vec2(p.x, p.y);
                float len2 = glm::dot(point.normal, point.normal);
                glm::vec3 normal = len2 > 1e-20f ? point.normal / std::sqrt(len2) : glm::vec3(0, 1, 0);
                // The procedural texture supplies leaf/bark variation. Flat
                // random tint per voxel would reintroduce the visible grid.
                vertices.push_back({p, normal, color, uv * uvScale});
            }
            indices.push_back(index);
        }
    }
}

}  // namespace VoxelSurface
