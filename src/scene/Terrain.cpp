#include "Terrain.h"

#include <utility>

Terrain::Terrain(VulkanContext& ctx, CommandContext& commands, TerrainGenerator::BuildResult build)
    : surface_(std::move(build.surface)),
      mesh_(uploadMesh(ctx, commands, build.mesh)),
      blas_(AccelerationStructure::buildBLAS(ctx, commands, mesh_)) {}

Mesh Terrain::uploadMesh(VulkanContext& ctx, CommandContext& commands,
                         const TerrainGenerator::MeshData& mesh) {
    std::vector<Vertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices)
        vertices.push_back({vertex.position, vertex.normal, glm::vec3(1.0f), vertex.uv});
    return Mesh(ctx, commands, vertices, mesh.indices);
}
