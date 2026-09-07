#include "scene/TerrainGenerator.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(float a, float b, const char* message, float tolerance = 3e-5f) {
    require(std::isfinite(a) && std::abs(a - b) <= tolerance, message);
}
template<class F> void rejects(F&& f) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid terrain input was accepted");
}

// Intersect vertical rays with the emitted triangles' planes. This checks the
// upload geometry against the query API without reproducing its grid lookup.
void checkMesh(const TerrainSurface& surface, const TerrainGenerator::MeshData& mesh) {
    const auto& hm = surface.heightmap();
    require(mesh.vertices.size() == hm.heights.size(), "wrong vertex count");
    require(mesh.indices.size() == size_t(hm.resolution - 1) * (hm.resolution - 1) * 6,
            "wrong index count");
    for (const auto& v : mesh.vertices) {
        near(surface.heightAt(v.position.x, v.position.z), v.position.y, "vertex contact mismatch");
        near(glm::length(v.normal), 1, "non-unit shading normal");
        require(v.normal.y > 0, "downward shading normal");
        near(v.uv.x, v.position.x / 3, "UV scale changed");
        near(v.uv.y, v.position.z / 3, "UV scale changed");
    }
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        for (size_t k = 0; k < 3; ++k)
            require(mesh.indices[i + k] < mesh.vertices.size(), "out-of-range index");
        auto a = mesh.vertices[mesh.indices[i]].position;
        auto b = mesh.vertices[mesh.indices[i + 1]].position;
        auto c = mesh.vertices[mesh.indices[i + 2]].position;
        auto n = glm::normalize(glm::cross(b - a, c - a));
        require(n.y > 0, "wrong triangle winding");
        // Shared edges and diagonals have a unique height even though either
        // adjacent face can supply the normal. Include both directions.
        for (auto p : {(a + b) * .5f, (b + c) * .5f, (c + a) * .5f})
            near(surface.heightAt(p.x, p.z), p.y, "edge/diagonal contact mismatch");
        for (glm::vec3 w : {glm::vec3(.2f, .3f, .5f), glm::vec3(.8f, .1f, .1f)}) {
            glm::vec3 p = a * w.x + b * w.y + c * w.z;
            float rayHeight = a.y - (n.x * (p.x - a.x) + n.z * (p.z - a.z)) / n.y;
            near(surface.heightAt(p.x, p.z), rayHeight, "ray/contact mismatch");
            require(glm::length(surface.contactNormalAt(p.x, p.z) - n) < 2e-5f,
                    "face/contact normal mismatch");
            auto smooth = glm::normalize(mesh.vertices[mesh.indices[i]].normal * w.x +
                                        mesh.vertices[mesh.indices[i + 1]].normal * w.y +
                                        mesh.vertices[mesh.indices[i + 2]].normal * w.z);
            require(glm::length(surface.shadingNormalAt(p.x, p.z) - smooth) < 3e-5f,
                    "interpolated shading normal mismatch");
        }
    }
}
}

int main() {
    // A folded quad exposes the old bilinear mismatch: diagonal height is 2,
    // whereas bilinear interpolation would incorrectly return 1.
    TerrainSurface folded({2, 2.0f, {0, 0, 0, 4}});
    near(folded.heightAt(0, 0), 2, "diagonal must follow the actual triangle");
    near(folded.heightAt(-.5f, .5f), 1, "first half of quad");
    near(folded.heightAt(.5f, -.5f), 1, "second half of quad");
    require(glm::length(folded.contactNormalAt(0, 0) - glm::normalize(glm::vec3(-2, 1, 0))) < 1e-6f,
            "diagonal ownership changed");
    require(glm::length(folded.contactNormalAt(.5f, -.5f) - folded.shadingNormalAt(.5f, -.5f)) > .1f,
            "contact and shading normals were conflated");
    auto foldedMesh = TerrainGenerator::buildMesh(folded);
    require(foldedMesh.indices == std::vector<uint32_t>({0, 2, 3, 0, 3, 1}),
            "legacy mesh diagonal changed");
    checkMesh(folded, foldedMesh);
    near(folded.heightAt(100, 0), 2, "positive edge clamp");
    near(folded.heightAt(0, 100), 2, "positive Z edge clamp");
    near(folded.heightAt(-100, 0), 0, "negative edge clamp");
    near(folded.heightAt(0, -100), 0, "negative Z edge clamp");
    near(folded.heightAt(100, 100), 4, "corner clamp");

    for (int fixture = 0; fixture < 4; ++fixture) {
        HeightmapGenerator::Heightmap hm{5, 7.5f, std::vector<float>(25)};
        for (int z = 0; z < 5; ++z) {
            for (int x = 0; x < 5; ++x) {
                float a = x - 2, b = z - 2;
                hm.heights[z * 5 + x] = fixture == 0 ? 3.0f :
                    fixture == 1 ? a * .5f - b :
                    fixture == 2 ? a * a + b * b : a * b;
            }
        }
        TerrainSurface surface(std::move(hm));
        checkMesh(surface, TerrainGenerator::buildMesh(surface));
    }

    for (uint32_t seed : TerrainGenerator::kRegressionSeeds) {
        TerrainGenerator::Settings settings;
        settings.seed = seed;
        auto first = TerrainGenerator::build(settings);
        auto repeat = TerrainGenerator::build(settings);
        auto legacy = HeightmapGenerator::generateHills(settings.resolution, settings.worldSize,
                                                         settings.amplitude, seed);
        require(first.surface.heightmap().heights == legacy.heights, "legacy landform changed");
        require(first.surface.heightmap().heights == repeat.surface.heightmap().heights,
                "non-deterministic heights");
        require(first.surface.shadingNormals() == repeat.surface.shadingNormals(),
                "non-deterministic normals");
        require(first.mesh.indices == repeat.mesh.indices, "non-deterministic indices");
        for (size_t i = 0; i < first.mesh.vertices.size(); ++i) {
            require(first.mesh.vertices[i].position == repeat.mesh.vertices[i].position &&
                    first.mesh.vertices[i].normal == repeat.mesh.vertices[i].normal &&
                    first.mesh.vertices[i].uv == repeat.mesh.vertices[i].uv,
                    "non-deterministic mesh");
        }
        checkMesh(first.surface, first.mesh);
    }
    TerrainGenerator::Settings oddGrid;
    oddGrid.resolution = 257;
    auto odd = TerrainGenerator::build(oddGrid);
    checkMesh(odd.surface, odd.mesh);

    // Vertex coordinates are rounded by the renderer's float position rule.
    // Queries at those exact positions must not leak into an adjacent cell.
    HeightmapGenerator::Heightmap steepGrid{256, 180, std::vector<float>(256 * 256)};
    for (size_t i = 0; i < steepGrid.heights.size(); ++i) steepGrid.heights[i] = float(i % 7) * 50;
    TerrainSurface steep(std::move(steepGrid));
    for (int z = 0; z < 256; z += 17) for (int x = 0; x < 256; x += 17) {
        auto p = steep.position(x, z);
        require(steep.heightAt(p.x, p.z) == p.y, "rendered grid vertex sampled a neighbouring height");
    }

    rejects([] { TerrainSurface s({1, 1, {0}}); });
    rejects([] { TerrainSurface s({2, 1, {0}}); });
    rejects([] { TerrainSurface s({2, 0, {0, 0, 0, 0}}); });
    rejects([] { TerrainSurface s({2, 1, {0, 0, 0, std::numeric_limits<float>::infinity()}}); });
    rejects([&] { folded.heightAt(std::numeric_limits<float>::quiet_NaN(), 0); });
    rejects([&] { folded.contactNormalAt(0, std::numeric_limits<float>::infinity()); });
    for (int invalid = 0; invalid < 5; ++invalid) {
        TerrainGenerator::Settings settings;
        if (invalid == 0) settings.resolution = 1;
        if (invalid == 1) settings.resolution = std::numeric_limits<int>::max();
        if (invalid == 2) settings.worldSize = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 3) settings.amplitude = -1;
        if (invalid == 4) settings.version = 999;
        rejects([&] { TerrainGenerator::build(settings); });
    }
}
