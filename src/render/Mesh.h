#pragma once

#include <vector>

#include "Buffer.h"
#include "CommandContext.h"
#include "Vertex.h"
#include "VulkanContext.h"

// Owns a device-local vertex + index buffer pair and knows how to bind and
// draw itself. All meshes in the prototype (terrain, boxes, shells, the
// loaded tank model) share this one representation.
class Mesh {
public:
    Mesh(VulkanContext& ctx, CommandContext& commands, const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);

    void bindAndDraw(VkCommandBuffer cmd) const;

    static Mesh cube(VulkanContext& ctx, CommandContext& commands, glm::vec3 color,
                      float size = 1.0f);

private:
    Buffer vertexBuffer_;
    Buffer indexBuffer_;
    uint32_t indexCount_;
};
