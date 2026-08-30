#include "Mesh.h"

#include <cmath>
#include <random>

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Appends a tapered cylindrical band (a frustum; use a small nonzero
// topRadius for a near-pointed cone tip, which avoids the degenerate
// zero-area triangles a literal topRadius of 0 would produce) between
// yBottom and yTop. Winding/normals verified via the same right-hand-rule
// check used for Mesh::cube's faces: for adjacent ring points at increasing
// angle with yTop > yBottom, triangles (bottom_i, top_i, top_next) and
// (bottom_i, top_next, bottom_next) are CCW as seen from outside.
void appendFrustum(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, float yBottom,
                    float yTop, float radiusBottom, float radiusTop, glm::vec3 color, int sides) {
    uint32_t base = static_cast<uint32_t>(vertices.size());

    for (int i = 0; i <= sides; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(sides) * 2.0f * kPi;
        float cx = std::cos(angle);
        float cz = std::sin(angle);
        glm::vec3 normal = glm::normalize(glm::vec3(cx, 0.0f, cz));
        vertices.push_back({glm::vec3(cx * radiusBottom, yBottom, cz * radiusBottom), normal, color});
        vertices.push_back({glm::vec3(cx * radiusTop, yTop, cz * radiusTop), normal, color});
    }

    for (int i = 0; i < sides; ++i) {
        uint32_t bottomCur = base + i * 2;
        uint32_t topCur = base + i * 2 + 1;
        uint32_t topNext = base + (i + 1) * 2 + 1;
        uint32_t bottomNext = base + (i + 1) * 2;
        indices.insert(indices.end(), {bottomCur, topCur, topNext, bottomCur, topNext, bottomNext});
    }
}

}  // namespace

Mesh::Mesh(VulkanContext& ctx, CommandContext& commands, const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices)
    : vertexBuffer_(Buffer::uploadDeviceLocal(ctx, commands, vertices.data(),
                                               sizeof(Vertex) * vertices.size(),
                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)),
      indexBuffer_(Buffer::uploadDeviceLocal(ctx, commands, indices.data(),
                                              sizeof(uint32_t) * indices.size(),
                                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT)),
      vertexCount_(static_cast<uint32_t>(vertices.size())),
      indexCount_(static_cast<uint32_t>(indices.size())) {}

void Mesh::bindAndDraw(VkCommandBuffer cmd) const {
    VkBuffer buffers[] = {vertexBuffer_.handle()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
}

Mesh Mesh::cube(VulkanContext& ctx, CommandContext& commands, glm::vec3 color, float size) {
    const float h = size * 0.5f;

    // Each face gets its own 4 vertices (rather than sharing the 8 cube
    // corners) so every face can have a flat per-face normal instead of an
    // averaged corner normal. Winding is CCW as seen from outside the cube,
    // verified against the right-hand rule for each face's outward normal --
    // required for VK_FRONT_FACE_COUNTER_CLOCKWISE + back-face culling to
    // work with the negative-viewport-height convention used elsewhere.
    struct Face {
        glm::vec3 normal;
        glm::vec3 corners[4];
    };
    const Face faces[6] = {
        {{1, 0, 0}, {{h, -h, -h}, {h, h, -h}, {h, h, h}, {h, -h, h}}},
        {{-1, 0, 0}, {{-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-h, -h, -h}}},
        {{0, 1, 0}, {{-h, h, -h}, {-h, h, h}, {h, h, h}, {h, h, -h}}},
        {{0, -1, 0}, {{-h, -h, h}, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}}},
        {{0, 0, 1}, {{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}},
        {{0, 0, -1}, {{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}},
    };

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(24);
    indices.reserve(36);

    for (const auto& face : faces) {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        for (const auto& corner : face.corners) {
            vertices.push_back({corner, face.normal, color});
        }
        indices.insert(indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }

    return Mesh(ctx, commands, vertices, indices);
}

Mesh Mesh::quad(VulkanContext& ctx, CommandContext& commands, glm::vec3 color) {
    constexpr float h = 0.5f;
    // Same corner order/winding as cube()'s +Y face (CCW as seen from
    // above), just flattened to Y=0.
    std::vector<Vertex> vertices = {
        {{-h, 0.0f, -h}, {0.0f, 1.0f, 0.0f}, color, {0.0f, 0.0f}},
        {{-h, 0.0f, h}, {0.0f, 1.0f, 0.0f}, color, {0.0f, 1.0f}},
        {{h, 0.0f, h}, {0.0f, 1.0f, 0.0f}, color, {1.0f, 1.0f}},
        {{h, 0.0f, -h}, {0.0f, 1.0f, 0.0f}, color, {1.0f, 0.0f}},
    };
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    return Mesh(ctx, commands, vertices, indices);
}

Mesh Mesh::rock(VulkanContext& ctx, CommandContext& commands, glm::vec3 baseColor, uint32_t seed) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    glm::vec3 base[12] = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t},  {0, 1, t},
        {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1},
    };
    const int faces[20][3] = {
        {0, 11, 5}, {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9},  {5, 11, 4},
        {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},  {3, 2, 6},  {3, 6, 8},
        {3, 8, 9},  {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},  {9, 8, 1},
    };

    // Jitter each of the 12 base vertices outward/inward along its own
    // direction from center, turning the perfect icosahedron into an
    // irregular lump. Deterministic per seed so the same seed always
    // produces the same rock (used to build a small, reusable pool of
    // distinct-looking variants -- see Application::rockMeshes_).
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> radiusJitter(0.7f, 1.35f);
    std::uniform_real_distribution<float> colorJitter(-0.05f, 0.05f);

    glm::vec3 deformed[12];
    for (int i = 0; i < 12; ++i) {
        deformed[i] = glm::normalize(base[i]) * radiusJitter(rng);
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(20 * 3);
    indices.reserve(20 * 3);

    for (const auto& face : faces) {
        glm::vec3 p0 = deformed[face[0]];
        glm::vec3 p1 = deformed[face[1]];
        glm::vec3 p2 = deformed[face[2]];

        // The icosahedron's own face winding isn't verified against this
        // project's CCW-outward convention, so derive it from the actual
        // (now-deformed) geometry instead of trusting the table: compute
        // the normal, and flip both it and the winding if it points inward.
        glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        glm::vec3 centroid = (p0 + p1 + p2) / 3.0f;
        if (glm::dot(normal, centroid) < 0.0f) {
            std::swap(p1, p2);
            normal = -normal;
        }

        glm::vec3 color = glm::clamp(baseColor + glm::vec3(colorJitter(rng)), glm::vec3(0.0f),
                                      glm::vec3(1.0f));

        uint32_t base_ = static_cast<uint32_t>(vertices.size());
        vertices.push_back({p0, normal, color});
        vertices.push_back({p1, normal, color});
        vertices.push_back({p2, normal, color});
        indices.insert(indices.end(), {base_ + 0, base_ + 1, base_ + 2});
    }

    return Mesh(ctx, commands, vertices, indices);
}

Mesh Mesh::dome(VulkanContext& ctx, CommandContext& commands, glm::vec3 color, float uvScale) {
    constexpr int kLatSegments = 12;
    constexpr int kLonSegments = 24;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(kLatSegments + 1) * (kLonSegments + 1));

    for (int lat = 0; lat <= kLatSegments; ++lat) {
        // theta=0 at the zenith (straight up), kPi/2 at the horizon --
        // clouds only need the upper hemisphere.
        float theta = (static_cast<float>(lat) / kLatSegments) * (kPi * 0.5f);
        float y = std::cos(theta);
        float ringRadius = std::sin(theta);
        for (int lon = 0; lon <= kLonSegments; ++lon) {
            float phi = (static_cast<float>(lon) / kLonSegments) * 2.0f * kPi;
            float x = ringRadius * std::cos(phi);
            float z = ringRadius * std::sin(phi);

            // Project onto a distant horizontal plane (divide by the
            // vertical component) instead of wrapping the texture around
            // the dome's own curvature -- reads as a flat cloud layer
            // receding toward the horizon rather than pinching at the
            // zenith. Clamp y so the projection doesn't blow up right at
            // the horizon ring.
            float denom = std::max(y, 0.05f);
            glm::vec2 uv = glm::vec2(x, z) / denom * uvScale;

            vertices.push_back({glm::vec3(x, y, z), glm::vec3(0.0f, -1.0f, 0.0f), color, uv});
        }
    }

    auto indexOf = [&](int lat, int lon) { return static_cast<uint32_t>(lat * (kLonSegments + 1) + lon); };
    for (int lat = 0; lat < kLatSegments; ++lat) {
        for (int lon = 0; lon < kLonSegments; ++lon) {
            uint32_t v00 = indexOf(lat, lon);
            uint32_t v01 = indexOf(lat, lon + 1);
            uint32_t v11 = indexOf(lat + 1, lon + 1);
            uint32_t v10 = indexOf(lat + 1, lon);
            // Both winding orders for each triangle -- see the comment on
            // Mesh::dome in the header for why.
            indices.insert(indices.end(), {v00, v01, v11, v00, v11, v01, v00, v11, v10, v00, v10, v11});
        }
    }

    return Mesh(ctx, commands, vertices, indices);
}

Mesh Mesh::tree(VulkanContext& ctx, CommandContext& commands, glm::vec3 trunkColor,
                 glm::vec3 canopyColor) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    appendFrustum(vertices, indices, 0.0f, 1.0f, 0.15f, 0.10f, trunkColor, 6);
    // Two overlapping cones for a layered pine-canopy silhouette.
    appendFrustum(vertices, indices, 0.8f, 2.0f, 0.9f, 0.05f, canopyColor, 8);
    appendFrustum(vertices, indices, 1.6f, 2.6f, 0.55f, 0.02f, canopyColor, 8);

    return Mesh(ctx, commands, vertices, indices);
}
