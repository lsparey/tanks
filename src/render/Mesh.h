#pragma once

#include <vector>
#include <array>

#include "Buffer.h"
#include "CommandContext.h"
#include "Vertex.h"
#include "VulkanContext.h"

namespace TreeGenerator { struct Tree; }

// Owns a device-local vertex + index buffer pair and knows how to bind and
// draw itself. All meshes in the prototype (terrain, boxes, shells, the
// loaded tank model) share this one representation.
class Mesh {
public:
    Mesh(VulkanContext& ctx, CommandContext& commands, const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices, float horizontalInscribedRadius = 0.0f);

    void bindAndDraw(VkCommandBuffer cmd) const;
    void bindAndDrawInstanced(VkCommandBuffer cmd, uint32_t instanceCount,
                              uint32_t firstInstance) const;
    void bindAndDrawIndirect(VkCommandBuffer cmd, VkBuffer commands, VkDeviceSize offset,
                             uint32_t count) const;
    void bindAndDrawRange(VkCommandBuffer cmd, const VkDrawIndexedIndirectCommand& draw) const;

    // Exposed so a BLAS can be built directly from this mesh's existing GPU
    // buffers (see AccelerationStructure::buildBLAS) without a second copy.
    const Buffer& vertexBuffer() const { return vertexBuffer_; }
    const Buffer& indexBuffer() const { return indexBuffer_; }
    uint32_t vertexCount() const { return vertexCount_; }
    uint32_t indexCount() const { return indexCount_; }
    // Radius of a centered circle kept inside the mesh's projected XZ
    // silhouette. Useful for irregular but roughly round props such as
    // boulders, where a fixed hand-tuned radius easily protrudes outside a
    // particular procedural variant.
    float horizontalInscribedRadius() const { return horizontalInscribedRadius_; }

    // Encloses all uploaded vertices in XZ, for scenery route reservations.
    float horizontalBoundingRadius() const { return horizontalBoundingRadius_; }

    static Mesh cube(VulkanContext& ctx, CommandContext& commands, glm::vec3 color,
                      float size = 1.0f);

    // A flat 1x1 quad in the local XZ plane (Y=0, normal +Y), UV spanning
    // 0..1 -- used for ground decals (see TrackMark), which scale it to the
    // desired footprint via their world matrix rather than baking a size in.
    static Mesh quad(VulkanContext& ctx, CommandContext& commands, glm::vec3 color);

    // All tree materials and LODs share one species-specific skeleton and
    // spray layout. Rounded voxel bark is supplemented by thin tapered
    // twigs; oak has separate lobed laminae, pine/ash retain voxel sprays.
    // Leaf LOD 3 contains inset, flattened shadow proxies.
    struct Geometry {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };
    // CPU-only, independent builds. Upload the returned arrays using the
    // Mesh constructor on the render thread that owns the command pool.
    static Geometry treeBarkGeometry(glm::vec3 tint, const TreeGenerator::Tree& tree, int lod = 0);
    static Geometry treeLeafGeometry(glm::vec3 tint, const TreeGenerator::Tree& tree, int lod = 0);
    // Low-cost visible LOD 2. Original geometry above remains available to
    // shadow maps/ray proxies and to the matched benchmark baseline.
    static Geometry treeDistantBarkGeometry(glm::vec3 tint, const TreeGenerator::Tree& tree);
    // Visible foliage grids: 0.18 model units for middle, 0.5 for far.
    static Geometry treeDistantLeafGeometry(glm::vec3 tint, const TreeGenerator::Tree& tree,
                                            float voxelSize = .5f);
    struct FoliageGroup {
        glm::vec3 center{0};
        float radius = 0;
        float cullRadius = 0; // includes the cheaper far mesh; radius retains the LOD policy
        uint32_t seed = 0;
        std::array<VkDrawIndexedIndirectCommand, 3> levels{};
        VkDrawIndexedIndirectCommand shadowMedium{}; // original LOD 1
        VkDrawIndexedIndirectCommand shadowFar{}; // original LOD 2, unchanged shadows/baseline
    };
    struct FoliageGeometry {
        Geometry mesh;
        std::vector<FoliageGroup> groups;
    };
    static FoliageGeometry treeFoliageGeometry(glm::vec3 tint, const TreeGenerator::Tree& tree);

    // A procedural boulder: a subdivided icosahedron with
    // broad, ridged, and fine layers of fractal displacement for an
    // irregular silhouette reminiscent of the canopy's nested forms.
    // Smooth averaged normals, slight per-face color jitter around
    // baseColor, and a spherical UV so
    // an actual rock texture (see Application's rockMaterialSet_) wraps
    // around it instead of flat vertex color. Different seeds give visibly
    // different rocks from the same call, so a handful of variants (see
    // Application's rockMeshes_) reads as varied rubble rather than the
    // same shape copy-pasted everywhere.
    // subdivisions defaults to the full 1280-face version; callers drawing
    // tiny scree can request a cheaper LOD with the same overall shape.
    // radiusScale is normally 1; ray-only proxies use a modest inset so
    // their coarse triangles remain inside the rendered surface.
    // angularity in [0,1] shifts the form from a rounded water-worn boulder
    // (0, the default) toward flatter, ridged, fracture-lined scree (1) for
    // freshly exposed rock on eroded scars.
    static Mesh rock(VulkanContext& ctx, CommandContext& commands, glm::vec3 baseColor,
                      uint32_t seed, int subdivisions = 3, float radiusScale = 1.0f,
                      float angularity = 0.0f);

    // A unit-radius sky sphere, meant to be scaled up and
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
    // built from an oriented-frustum helper,
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

    // An irregular convex fragment for explosion debris: an icosahedron with
    // per-vertex radial jitter and anisotropic proportions, flat-shaded so
    // each facet catches light like a fracture plane. Each face randomly
    // carries either the exterior color (the object's old painted/weathered
    // surface) or the fracture color (the freshly broken interior), which is
    // what makes a tumbling fragment read as a splinter of *something*
    // rather than a solid-colored lump. Fits within radius ~0.5 like cube();
    // per-instance size comes from DebrisParticle::baseScale. `seed` varies
    // the jitter/face palette per variant, `proportions` elongates (e.g.
    // z-major for wood splinters).
    static Mesh shard(VulkanContext& ctx, CommandContext& commands, glm::vec3 exteriorColor,
                      glm::vec3 fractureColor, uint32_t seed,
                      glm::vec3 proportions = glm::vec3(1.0f));

    // A small low bush: three overlapping gently-jittered blobs like
    // blobCluster, but low to the ground and opaque/lit (not alpha-blended)
    // -- same appendLeafBlob shape foliage clusters use, just without the
    // trunk/branch structure underneath, so it reads as a shrub rather than
    // a tree. `seed` varies the blobs' own jitter per variant (see
    // Application's shrubMeshes_, which builds a handful of these) the same
    // way Mesh::rock's seed does.
    static Mesh shrub(VulkanContext& ctx, CommandContext& commands, glm::vec3 color, uint32_t seed);

    // A small ground-level tuft of a handful of gently-leaning blade
    // triangles around a shared base point, for near-field grass clumps
    // scattered across grassy terrain (see Application::spawnGrassClumps).
    // Real geometry, not a billboard card -- `seed` varies blade count/
    // placement/lean per variant the same way Mesh::shrub's seed does.
    // A tall waterside reed/rush clump: straighter, taller blades than
    // grassClump plus a few dark bulrush seed heads. Placed along generated
    // shorelines (see Application::spawnReeds); drawn through the same
    // instanced near-field path as grass tufts.
    static Mesh reedClump(VulkanContext& ctx, CommandContext& commands, glm::vec3 color,
                           uint32_t seed);
    static Mesh grassClump(VulkanContext& ctx, CommandContext& commands, glm::vec3 color,
                           uint32_t seed);

private:
    Buffer vertexBuffer_;
    Buffer indexBuffer_;
    uint32_t vertexCount_;
    uint32_t indexCount_;
    float horizontalInscribedRadius_ = 0.0f;
    float horizontalBoundingRadius_ = 0.0f;
};
