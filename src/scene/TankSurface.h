#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "../render/Vertex.h"

namespace TankSurface {

// Tank vertices previously carried a uniform white tint. Reuse those three
// channels for distances to each triangle's convex feature edges, avoiding
// an extra attribute on every terrain/scenery vertex. basic.frag decodes
// them only for tank materials. Positions, normals, UVs and triangles stay
// unchanged; duplicate corners permit independent per-triangle distances.
inline void bakeEdgeDistances(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    using Point = std::array<long long, 3>;
    using Edge = std::array<Point, 2>;
    struct Face {
        glm::vec3 normal{0.0f};
        glm::vec3 center{0.0f};
        std::array<bool, 3> worn{};
    };
    struct Use { size_t face; int opposite; };
    auto point = [](glm::vec3 p) -> Point {
        return {std::llround(p.x * 10000.0), std::llround(p.y * 10000.0),
                std::llround(p.z * 10000.0)};
    };
    std::vector<Face> faces(indices.size() / 3);
    std::map<Edge, std::vector<Use>> edges;
    for (size_t f = 0; f < faces.size(); ++f) {
        const Vertex& a = vertices[indices[f * 3]];
        const Vertex& b = vertices[indices[f * 3 + 1]];
        const Vertex& c = vertices[indices[f * 3 + 2]];
        glm::vec3 cross = glm::cross(b.position - a.position, c.position - a.position);
        float area = glm::length(cross);
        if (area < 1e-8f) continue;
        faces[f].normal = cross / area;
        // Imported normals establish the outward side even for reversed
        // index winding in an older model export.
        if (glm::dot(faces[f].normal, a.normal + b.normal + c.normal) < 0.0f)
            faces[f].normal = -faces[f].normal;
        faces[f].center = (a.position + b.position + c.position) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            Point p = point(vertices[indices[f * 3 + (i + 1) % 3]].position);
            Point q = point(vertices[indices[f * 3 + (i + 2) % 3]].position);
            if (q < p) std::swap(p, q);
            edges[{p, q}].push_back({f, i});
        }
    }
    for (const auto& [edge, uses] : edges) {
        (void)edge;
        if (uses.size() != 2) continue;  // open/non-manifold edges stay unpainted
        Face& a = faces[uses[0].face];
        Face& b = faces[uses[1].face];
        if (glm::dot(a.normal, b.normal) > 0.70f) continue;
        if (glm::dot(a.normal, b.center - a.center) >= -1e-5f ||
            glm::dot(b.normal, a.center - b.center) >= -1e-5f) continue;
        a.worn[uses[0].opposite] = true;
        b.worn[uses[1].opposite] = true;
    }
    std::vector<Vertex> baked;
    baked.reserve(indices.size());
    for (size_t f = 0; f < faces.size(); ++f) {
        std::array<Vertex, 3> triangle = {vertices[indices[f * 3]], vertices[indices[f * 3 + 1]],
                                         vertices[indices[f * 3 + 2]]};
        for (Vertex& v : triangle) v.color = glm::vec3(1000.0f);
        for (int edge = 0; edge < 3; ++edge) {
            if (!faces[f].worn[edge]) continue;
            glm::vec3 a = triangle[(edge + 1) % 3].position;
            glm::vec3 b = triangle[(edge + 2) % 3].position;
            float height = glm::length(glm::cross(triangle[edge].position - a, b - a)) /
                           std::max(glm::length(b - a), 1e-8f);
            for (int corner = 0; corner < 3; ++corner)
                triangle[corner].color[edge] = corner == edge ? height : 0.0f;
        }
        baked.insert(baked.end(), triangle.begin(), triangle.end());
    }
    vertices = std::move(baked);
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<uint32_t>(i);
}

}  // namespace TankSurface
