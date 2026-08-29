#include "Mesh.h"

Mesh::Mesh(VulkanContext& ctx, CommandContext& commands, const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices)
    : vertexBuffer_(Buffer::uploadDeviceLocal(ctx, commands, vertices.data(),
                                               sizeof(Vertex) * vertices.size(),
                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)),
      indexBuffer_(Buffer::uploadDeviceLocal(ctx, commands, indices.data(),
                                              sizeof(uint32_t) * indices.size(),
                                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT)),
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
