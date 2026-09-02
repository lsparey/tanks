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

    // A procedural tree, built as a recursive fractal branching structure
    // (trunk splits into a few branches, each of which splits again,
    // several levels deep) rather than a fixed trunk+2-cones shape --
    // returned as two separate meshes since bark and foliage need
    // different textures/materials: treeBark is the trunk/branch skeleton
    // (cylindrical UV for a tiling bark texture), treeLeaves is the small
    // cone clusters at each branch tip (for a foliage texture). Call both
    // with the same `seed` to get the matching pair for one tree -- the
    // branch structure (and therefore where the leaf clusters end up) is
    // fully determined by seed, so two calls with the same seed reproduce
    // the identical skeleton.
    static Mesh treeBark(VulkanContext& ctx, CommandContext& commands, glm::vec3 tint, uint32_t seed);
    static Mesh treeLeaves(VulkanContext& ctx, CommandContext& commands, glm::vec3 tint, uint32_t seed);

    // A procedural boulder: a once-subdivided icosahedron (80 faces) with
    // each vertex's radius displaced by multi-octave (fractal) 3D noise for
    // an irregular, organic-looking lumpy silhouette -- notably more
    // detailed than a single random jitter per base icosahedron vertex.
    // Flat per-face normals (same faceted-shading approach as cube()), a
    // slight per-face color jitter around baseColor, and a spherical UV so
    // an actual rock texture (see Application's rockMaterialSet_) wraps
    // around it instead of flat vertex color. Different seeds give visibly
    // different rocks from the same call, so a handful of variants (see
    // Application's rockMeshes_) reads as varied rubble rather than the
    // same shape copy-pasted everywhere.
    static Mesh rock(VulkanContext& ctx, CommandContext& commands, glm::vec3 baseColor,
                      uint32_t seed);

    // A unit-radius upper hemisphere (Y >= 0), meant to be scaled up and
    // recentered on the camera each frame as a sky backdrop for clouds
    // (see Application's cloud dome). UV is a "project onto a distant
    // horizontal plane" mapping (divide the local XZ direction by Y)
    // rather than a spherical wrap, so the cloud texture reads as a flat
    // layer receding toward the horizon instead of pinching at the zenith.
    // Built double-sided (both triangle winding orders) since it's only
    // ever seen from inside and getting the "inward-facing" winding right
    // by hand isn't worth the risk for a purely decorative element -- the
    // pipeline's cull mode is otherwise fixed for every other mesh.
    static Mesh dome(VulkanContext& ctx, CommandContext& commands, glm::vec3 color,
                      float uvScale);

    // An artillery shell: a short cylindrical body plus a tapered nose,
    // built from the same oriented-frustum helper tree branches use,
    // oriented along local +Z ("forward", matching Tank/fragTangent's
    // convention) with the nose pointing +Z -- see Projectile::worldMatrix,
    // which builds a basis mapping local +Z to the shell's actual flight
    // direction. Unit-ish local dimensions (not full world scale); the
    // instance-facing scale is baked into Projectile's own dimensions
    // rather than here, same reasoning as quad()'s doc comment.
    static Mesh shell(VulkanContext& ctx, CommandContext& commands, glm::vec3 color);

    // A soft, irregular blob -- three overlapping gently-jittered lumps
    // (same low-poly rounded shape appendLeafBlob uses for foliage
    // clusters), meant to be drawn alpha-blended and unlit. Generic enough
    // to reuse for anything that wants a rounded, non-geometric puffy
    // shape rather than a hard-edged primitive: smoke (muzzle blast/shell
    // trail, see SmokePuff.h, tinted grey) and the explosion flash (see
    // ImpactEffect, tinted bright warm white) both use this one shared
    // mesh -- they only differ in color (baked in here) and per-instance
    // scale/position/opacity, not shape.
    static Mesh blobCluster(VulkanContext& ctx, CommandContext& commands, glm::vec3 color);

    // A small low bush: three overlapping gently-jittered blobs like
    // blobCluster, but low to the ground and opaque/lit (not alpha-blended)
    // -- same appendLeafBlob shape foliage clusters use, just without the
    // trunk/branch structure underneath, so it reads as a shrub rather than
    // a tree. `seed` varies the blobs' own jitter per variant (see
    // Application's shrubMeshes_, which builds a handful of these) the same
    // way Mesh::rock's seed does.
    static Mesh shrub(VulkanContext& ctx, CommandContext& commands, glm::vec3 color, uint32_t seed);

private:
    Buffer vertexBuffer_;
    Buffer indexBuffer_;
    uint32_t vertexCount_;
    uint32_t indexCount_;
};
