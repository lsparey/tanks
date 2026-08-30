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

    // Exposed so a BLAS can be built directly from this mesh's existing GPU
    // buffers (see AccelerationStructure::buildBLAS) without a second copy.
    const Buffer& vertexBuffer() const { return vertexBuffer_; }
    const Buffer& indexBuffer() const { return indexBuffer_; }
    uint32_t vertexCount() const { return vertexCount_; }
    uint32_t indexCount() const { return indexCount_; }

    static Mesh cube(VulkanContext& ctx, CommandContext& commands, glm::vec3 color,
                      float size = 1.0f);

    // A flat 1x1 quad in the local XZ plane (Y=0, normal +Y), UV spanning
    // 0..1 -- used for ground decals (see TrackMark), which scale it to the
    // desired footprint via their world matrix rather than baking a size in.
    static Mesh quad(VulkanContext& ctx, CommandContext& commands, glm::vec3 color);

    // A simple procedural pine tree: a tapered trunk plus two stacked,
    // overlapping cones for the canopy -- hand-built geometry, same spirit
    // as cube().
    static Mesh tree(VulkanContext& ctx, CommandContext& commands, glm::vec3 trunkColor,
                      glm::vec3 canopyColor);

    // A procedural boulder: an icosahedron with each vertex's radius
    // jittered by `seed` for an irregular, lumpy silhouette, flat per-face
    // normals (same faceted-shading approach as cube()), and a slight
    // per-face color jitter around baseColor. Different seeds give visibly
    // different rocks from the same call, so a handful of variants (see
    // Application's rockMeshes_) reads as varied rubble rather than the
    // same shape copy-pasted everywhere.
    static Mesh rock(VulkanContext& ctx, CommandContext& commands, glm::vec3 baseColor,
                      uint32_t seed);

private:
    Buffer vertexBuffer_;
    Buffer indexBuffer_;
    uint32_t vertexCount_;
    uint32_t indexCount_;
};
