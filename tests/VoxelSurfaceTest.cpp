#include "render/VoxelSurface.h"

#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
using Point = std::array<long long, 3>;
Point key(glm::vec3 p) {
    return {std::llround(p.x * 100000), std::llround(p.y * 100000), std::llround(p.z * 100000)};
}

void validate(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    require(!vertices.empty() && indices.size() % 3 == 0, "Missing surface");
    std::map<std::array<Point, 2>, std::pair<int, int>> edges;
    std::map<Point, glm::vec3> normals;
    for (const Vertex& vertex : vertices) {
        require(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) &&
                std::isfinite(vertex.position.z), "Non-finite position");
        require(std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y), "Non-finite UV");
        require(std::abs(glm::length(vertex.normal) - 1.0f) < 1e-5f, "Non-unit normal");
        auto [it, inserted] = normals.emplace(key(vertex.position), vertex.normal);
        require(inserted || glm::length(it->second - vertex.normal) < 1e-5f,
                "Normal seam between texture projections");
    }
    for (size_t i = 0; i < indices.size(); i += 3) {
        for (int j = 0; j < 3; ++j) require(indices[i + j] < vertices.size(), "Invalid index");
        const auto& a = vertices[indices[i]];
        const auto& b = vertices[indices[i + 1]];
        const auto& c = vertices[indices[i + 2]];
        glm::vec3 normal = glm::cross(b.position - a.position, c.position - a.position);
        require(glm::length(normal) > 1e-8f, "Degenerate triangle");
        require(glm::dot(normal, a.normal + b.normal + c.normal) > 0, "Reversed triangle");
        for (int j = 0; j < 3; ++j) {
            Point p = key(vertices[indices[i + j]].position);
            Point q = key(vertices[indices[i + (j + 1) % 3]].position);
            int direction = p < q ? 1 : -1;
            if (q < p) std::swap(p, q);
            auto& edge = edges[{p, q}];
            ++edge.first;
            edge.second += direction;
        }
    }
    for (const auto& [edge, uses] : edges) {
        (void)edge;
        require(uses.first == 2 && uses.second == 0, "Open or inconsistently wound surface");
    }
}

void singleCell() {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    VoxelSurface::appendMesh(vertices, indices, {}, 1, glm::vec3(1), 1);
    require(vertices.empty() && indices.empty(), "Empty field emitted geometry");
    VoxelSurface::appendMesh(vertices, indices, {{0, 0, 0}}, 1, glm::vec3(1), 1);
    require(indices.size() == 36 && vertices.size() <= 24, "Single voxel geometry budget");
    validate(vertices, indices);
    for (const auto& vertex : vertices) {
        require(glm::dot(vertex.normal, vertex.position - glm::vec3(0.5f)) > 0,
                "Single voxel normal faces inward");
        require(vertex.position.x > 0 && vertex.position.x < 1 &&
                vertex.position.y > 0 && vertex.position.y < 1 &&
                vertex.position.z > 0 && vertex.position.z < 1, "Corner was not rounded");
    }
    size_t firstVertices = vertices.size(), firstIndices = indices.size();
    VoxelSurface::appendMesh(vertices, indices, {{-3, 2, -1}}, 0.5f, glm::vec3(0.8f), 2);
    for (size_t i = firstIndices; i < indices.size(); ++i)
        require(indices[i] >= firstVertices, "Appending reused an earlier mesh's indices");
    validate(vertices, indices);
}

void sphereAndBranch() {
    VoxelSurface::Cells cells;
    constexpr float radius = 6.5f;
    for (int z = -7; z <= 7; ++z)
        for (int y = -7; y <= 7; ++y)
            for (int x = -7; x <= 7; ++x)
                if (glm::length(glm::vec3(x, y, z) + 0.5f) < radius) cells.insert({x, y, z});
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    VoxelSurface::appendMesh(vertices, indices, cells, 0.1f, glm::vec3(1), 1.4f);
    validate(vertices, indices);
    for (const auto& vertex : vertices) {
        require(std::abs(glm::length(vertex.position) - radius * 0.1f) < 0.1f,
                "Smoothing moved the sphere by more than a voxel");
        require(glm::dot(glm::normalize(vertex.position), vertex.normal) > 0.8f,
                "Sphere still has axis-aligned face normals");
    }
    size_t exposed = 0;
    for (glm::ivec3 cell : cells)
        for (int axis = 0; axis < 3; ++axis)
            for (int sign : {-1, 1}) {
                glm::ivec3 neighbor = cell;
                neighbor[axis] += sign;
                exposed += !cells.contains(neighbor);
            }
    require(indices.size() == exposed * 6, "Smoothing added triangles");
    require(vertices.size() < exposed * 4, "Surface vertices were not shared");

    // A one-cell-wide upright twig must stay closed and retain its length.
    cells.clear();
    for (int y = -4; y < 5; ++y) cells.insert({-2, y, 1});
    vertices.clear();
    indices.clear();
    VoxelSurface::appendMesh(vertices, indices, cells, 0.1f, glm::vec3(1), 2.2f);
    validate(vertices, indices);
    float low = 1, high = -1;
    for (const auto& vertex : vertices) {
        low = std::min(low, vertex.position.y);
        high = std::max(high, vertex.position.y);
    }
    require(high - low > 0.8f, "Thin branch collapsed during smoothing");
}

}  // namespace

int main() {
    singleCell();
    sphereAndBranch();
}
