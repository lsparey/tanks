#include "BoundaryGenerator.h"

#include <vector>

#include <glm/glm.hpp>

#include "../render/Vertex.h"
#include "Terrain.h"

namespace {

// Matches Terrain's own grass tiling scale (see Terrain.cpp) -- keeps both
// boundary textures reading at a similar real-world texel density to the
// ground they sit on/above instead of looking stretched or overly tiled by
// comparison.
constexpr float kTextureRepeatsPerUnit = 1.0f / 3.0f;

// Samples per side of the square perimeter -- chosen to land close to
// Terrain's own 64x64 grid spacing (~2.8 world units per cell at the
// current 180-unit worldSize) so the ring follows the heightmap's hills
// about as faithfully as the terrain mesh itself does, without needing to
// know the exact grid resolution.
constexpr int kSegmentsPerSide = 48;

// Walks the square perimeter [-halfExtent, halfExtent]^2 counter-clockwise
// as seen from above (matching Terrain::buildMesh's winding convention),
// `segmentsPerSide` samples per edge. The returned list is a closed loop:
// the last point duplicates the first, so callers can get a "next point"
// for every entry without a modulo, while the actual per-vertex loop below
// stops one short of that duplicate and wraps via modulo when indexing.
std::vector<glm::vec2> perimeterPoints(float halfExtent, int segmentsPerSide) {
    std::vector<glm::vec2> pts;
    glm::vec2 corners[4] = {
        {-halfExtent, -halfExtent},
        {halfExtent, -halfExtent},
        {halfExtent, halfExtent},
        {-halfExtent, halfExtent},
    };
    for (int side = 0; side < 4; ++side) {
        glm::vec2 a = corners[side];
        glm::vec2 b = corners[(side + 1) % 4];
        for (int s = 0; s < segmentsPerSide; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(segmentsPerSide);
            pts.push_back(glm::mix(a, b, t));
        }
    }
    pts.push_back(pts[0]);
    return pts;
}

}  // namespace

std::unique_ptr<Mesh> BoundaryGenerator::buildLineMesh(VulkanContext& ctx, CommandContext& commands,
                                                        const Terrain& terrain, float halfExtent) {
    constexpr float kLineWidth = 0.14f;
    // Lifted slightly above the true terrain surface, same idea as a
    // fading ground decal, to avoid z-fighting with the terrain mesh below.
    constexpr float kGroundOffset = 0.03f;

    std::vector<glm::vec2> pts = perimeterPoints(halfExtent, kSegmentsPerSide);
    size_t pointCount = pts.size() - 1;  // unique perimeter points, excluding the closing duplicate

    std::vector<Vertex> vertices;
    vertices.reserve(pointCount * 2);
    const glm::vec3 white(1.0f);  // the texture itself carries the red color

    float distanceAlong = 0.0f;
    for (size_t i = 0; i < pointCount; ++i) {
        glm::vec2 p = pts[i];
        glm::vec2 prev = pts[(i + pointCount - 1) % pointCount];
        glm::vec2 next = pts[i + 1];
        glm::vec2 tangent = glm::normalize(next - prev);

        glm::vec3 up = terrain.normalAt(p.x, p.y);
        glm::vec3 forward(tangent.x, 0.0f, tangent.y);
        glm::vec3 right = glm::normalize(glm::cross(up, forward)) * (kLineWidth * 0.5f);

        glm::vec2 leftXZ = p - glm::vec2(right.x, right.z);
        glm::vec2 rightXZ = p + glm::vec2(right.x, right.z);
        float leftY = terrain.heightAt(leftXZ.x, leftXZ.y) + kGroundOffset;
        float rightY = terrain.heightAt(rightXZ.x, rightXZ.y) + kGroundOffset;

        float v = distanceAlong * kTextureRepeatsPerUnit;
        vertices.push_back({{leftXZ.x, leftY, leftXZ.y}, up, white, {0.0f, v}});
        vertices.push_back({{rightXZ.x, rightY, rightXZ.y}, up, white, {1.0f, v}});

        distanceAlong += glm::length(next - p);
    }

    // Defensive winding check (same spirit as Mesh::rock's per-face check
    // and appendOrientedFrustum's in Mesh.cpp): rather than hand-deriving
    // whether (left, right, cross) is CCW from above for every possible
    // terrain slope, just build the first quad, measure its actual face
    // normal, and flip every quad's winding together if it disagrees with
    // the ring's own (unambiguously upward-ish) vertex normal.
    bool flip = false;
    if (pointCount >= 2) {
        glm::vec3 p0 = vertices[0].position;
        glm::vec3 p1 = vertices[1].position;
        glm::vec3 p2 = vertices[3].position;
        flip = glm::dot(glm::normalize(glm::cross(p1 - p0, p2 - p0)), vertices[0].normal) < 0.0f;
    }

    std::vector<uint32_t> indices;
    indices.reserve(pointCount * 6);
    for (size_t i = 0; i < pointCount; ++i) {
        size_t next = (i + 1) % pointCount;
        uint32_t base = static_cast<uint32_t>(i * 2);
        uint32_t nbase = static_cast<uint32_t>(next * 2);
        if (!flip) {
            indices.insert(indices.end(), {base, base + 1, nbase + 1, base, nbase + 1, nbase});
        } else {
            indices.insert(indices.end(), {base, nbase + 1, base + 1, base, nbase, nbase + 1});
        }
    }

    return std::make_unique<Mesh>(ctx, commands, vertices, indices);
}

std::unique_ptr<Mesh> BoundaryGenerator::buildWallMesh(VulkanContext& ctx, CommandContext& commands,
                                                        const Terrain& terrain, float halfExtent,
                                                        float wallHeight) {
    std::vector<glm::vec2> pts = perimeterPoints(halfExtent, kSegmentsPerSide);
    size_t pointCount = pts.size() - 1;

    std::vector<Vertex> vertices;
    vertices.reserve(pointCount * 2);
    const glm::vec3 white(1.0f);  // the texture itself carries the red glow color
    const glm::vec3 outNormal(0.0f, 0.0f, 1.0f);  // unused by the unlit shader path; any unit vector works

    float distanceAlong = 0.0f;
    for (size_t i = 0; i < pointCount; ++i) {
        glm::vec2 p = pts[i];
        glm::vec2 next = pts[i + 1];
        float baseY = terrain.heightAt(p.x, p.y);

        // v=0 at the base, v=1 at the top -- matches
        // BoundaryTextureGenerator::generateWall's alpha gradient (opaque
        // near v=0, faded out by v=1).
        float u = distanceAlong * kTextureRepeatsPerUnit;
        vertices.push_back({{p.x, baseY, p.y}, outNormal, white, {u, 0.0f}});
        vertices.push_back({{p.x, baseY + wallHeight, p.y}, outNormal, white, {u, 1.0f}});

        distanceAlong += glm::length(next - p);
    }

    // Double-sided (both winding orders per quad) rather than a single
    // hand-derived winding, same reasoning as Mesh::dome -- this can be
    // seen equally plausibly from inside or outside the play area.
    std::vector<uint32_t> indices;
    indices.reserve(pointCount * 12);
    for (size_t i = 0; i < pointCount; ++i) {
        size_t next = (i + 1) % pointCount;
        uint32_t bottomCur = static_cast<uint32_t>(i * 2);
        uint32_t topCur = bottomCur + 1;
        uint32_t bottomNext = static_cast<uint32_t>(next * 2);
        uint32_t topNext = bottomNext + 1;
        indices.insert(indices.end(), {bottomCur, topCur, topNext, bottomCur, topNext, bottomNext});
        indices.insert(indices.end(), {bottomCur, topNext, topCur, bottomCur, bottomNext, topNext});
    }

    return std::make_unique<Mesh>(ctx, commands, vertices, indices);
}
