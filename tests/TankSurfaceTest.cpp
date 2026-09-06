#include "scene/TankSurface.h"

#include <stdexcept>

void require(bool condition) {
    if (!condition) throw std::runtime_error("Tank surface edge regression");
}

Vertex vertex(glm::vec3 p, glm::vec3 n) { return {p, n, glm::vec3(0.95f), {0.2f, 0.7f}}; }

void checkWedge(bool convex) {
    // Separate vertices at the common edge reproduce UV/normal seams in
    // imported meshes. Welding must identify the physical edge anyway.
    float y = convex ? -1.0f : 1.0f;
    glm::vec3 sideNormal(convex ? -1.0f : 1.0f, 0.0f, 0.0f);
    std::vector<Vertex> vertices = {
        vertex({0, 0, 0}, {0, 1, 0}), vertex({1, 0, 0}, {0, 1, 0}), vertex({0, 0, 1}, {0, 1, 0}),
        vertex({0, 0, 0}, sideNormal), vertex({0, 0, 1}, sideNormal), vertex({0, y, 0}, sideNormal)};
    auto original = vertices;
    std::vector<uint32_t> indices{0, 1, 2, 3, 4, 5};
    TankSurface::bakeEdgeDistances(vertices, indices);
    require(vertices.size() == original.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        require(vertices[i].position == original[i].position);
        require(vertices[i].normal == original[i].normal && vertices[i].uv == original[i].uv);
        require(indices[i] == i);
    }
    for (size_t face = 0; face < 2; ++face) {
        int activeEdges = 0;
        for (int channel = 0; channel < 3; ++channel) {
            float sum = 0;
            for (int corner = 0; corner < 3; ++corner)
                sum += vertices[face * 3 + corner].color[channel];
            if (sum < 1000.0f) {
                ++activeEdges;
                require(std::abs(sum - 1.0f) < 1e-5f);  // unit altitude, zero at edge endpoints
            }
        }
        require(activeEdges == (convex ? 1 : 0));
    }
}

int main() {
    checkWedge(true);
    checkWedge(false);
    // A triangulated flat plate must not acquire a diagonal wear stripe,
    // and its open boundary is intentionally not treated as a convex edge.
    std::vector<Vertex> vertices = {
        vertex({0, 0, 0}, {0, 1, 0}), vertex({1, 0, 0}, {0, 1, 0}),
        vertex({1, 0, 1}, {0, 1, 0}), vertex({0, 0, 1}, {0, 1, 0})};
    std::vector<uint32_t> indices{0, 1, 2, 0, 2, 3};
    TankSurface::bakeEdgeDistances(vertices, indices);
    require(vertices.size() == 6);
    for (const auto& v : vertices) require(v.color == glm::vec3(1000.0f));
}
