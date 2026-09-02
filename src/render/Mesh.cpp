#include "Mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <random>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Appends a tapered cylindrical band (a frustum; use a small nonzero
// topRadius for a near-pointed cone tip, which avoids the degenerate
// zero-area triangles a literal topRadius of 0 would produce) oriented
// along an arbitrary `dir` from `base` -- a generalization of a simple
// vertical frustum, used for tree branches that fan out in different
// directions. UV.u wraps around the circumference (for a tiling bark
// texture); UV.v runs 0 at the base to `length * vRepeat` at the tip, so
// bark tiles at a consistent real-world scale regardless of a given
// branch's length.
//
// The (right, fwd) basis used to build the ring isn't checked for handedness
// against `dir` ahead of time -- rather than risk getting that reasoning
// wrong by hand (as very nearly happened while writing this), the winding
// is verified defensively after the fact: compute the first quad's actual
// face normal and compare it to the ring's own (unambiguously outward)
// radial normal, flipping every quad's winding if they disagree. Same
// defensive spirit as Mesh::rock's per-face winding check.
void appendOrientedFrustum(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                            glm::vec3 base, glm::vec3 dir, float length, float radiusBottom,
                            float radiusTop, glm::vec3 color, int sides, float vRepeat) {
    dir = glm::normalize(dir);
    glm::vec3 up = std::abs(dir.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(up, dir));
    glm::vec3 fwd = glm::cross(dir, right);

    glm::vec3 tip = base + dir * length;
    uint32_t startIdx = static_cast<uint32_t>(vertices.size());

    for (int i = 0; i <= sides; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(sides) * 2.0f * kPi;
        glm::vec3 ringDir = std::cos(angle) * right + std::sin(angle) * fwd;
        glm::vec3 normal = glm::normalize(ringDir);
        float u = static_cast<float>(i) / static_cast<float>(sides);
        vertices.push_back({base + ringDir * radiusBottom, normal, color, glm::vec2(u, 0.0f)});
        vertices.push_back({tip + ringDir * radiusTop, normal, color, glm::vec2(u, length * vRepeat)});
    }

    glm::vec3 p0 = vertices[startIdx].position;
    glm::vec3 p1 = vertices[startIdx + 1].position;
    glm::vec3 p2 = vertices[startIdx + 3].position;
    bool flip = glm::dot(glm::normalize(glm::cross(p1 - p0, p2 - p0)), vertices[startIdx].normal) < 0.0f;

    for (int i = 0; i < sides; ++i) {
        uint32_t bottomCur = startIdx + i * 2;
        uint32_t topCur = startIdx + i * 2 + 1;
        uint32_t topNext = startIdx + (i + 1) * 2 + 1;
        uint32_t bottomNext = startIdx + (i + 1) * 2;
        if (!flip) {
            indices.insert(indices.end(), {bottomCur, topCur, topNext, bottomCur, topNext, bottomNext});
        } else {
            indices.insert(indices.end(), {bottomCur, topNext, topCur, bottomCur, bottomNext, topNext});
        }
    }
}

// Cheap value noise used only for baked-once vertex displacement (rock
// bumps, below) -- never sampled through a repeating texture, so unlike the
// texture generators' noise this has no need to be seamlessly tileable.
float hashF(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

float smoothNoise2D(float x, float y) {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    float tx = x - static_cast<float>(x0);
    float ty = y - static_cast<float>(y0);
    float sx = tx * tx * (3.0f - 2.0f * tx);
    float sy = ty * ty * (3.0f - 2.0f * ty);
    float n00 = hashF(x0, y0);
    float n10 = hashF(x0 + 1, y0);
    float n01 = hashF(x0, y0 + 1);
    float n11 = hashF(x0 + 1, y0 + 1);
    float nx0 = n00 + sx * (n10 - n00);
    float nx1 = n01 + sx * (n11 - n01);
    return nx0 + sy * (nx1 - nx0);
}

// Cheap seamless-on-a-sphere approximation to 3D noise: average three 2D
// noise lookups on different coordinate-plane projections of the input
// point. Avoids the meridian seam a spherical (theta, phi) angle
// parameterization would have, at the cost of not being a "real" 3D noise
// (good enough for baked-once displacement on a small, already-irregular
// boulder).
float noise3D(glm::vec3 p) {
    float xy = smoothNoise2D(p.x, p.y);
    float yz = smoothNoise2D(p.y, p.z);
    float zx = smoothNoise2D(p.z, p.x);
    return (xy + yz + zx) / 3.0f;
}

// Multi-octave ("fractal") 3D noise -- this is what gives the displaced
// rock its layered, organic-looking bumps (large dents plus fine surface
// roughness) instead of one uniform wobble.
float fractalNoise3D(glm::vec3 p, int octaves) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float total = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * noise3D(p * frequency);
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / total;
}

void subdivideIcosphere(std::vector<glm::vec3>& vertices,
                        std::vector<std::array<int, 3>>& faces) {
    std::map<std::pair<int, int>, int> midpointCache;
    auto midpoint = [&](int a, int b) {
        auto key = std::minmax(a, b);
        auto it = midpointCache.find(key);
        if (it != midpointCache.end()) return it->second;
        vertices.push_back(glm::normalize((vertices[a] + vertices[b]) * 0.5f));
        int index = static_cast<int>(vertices.size()) - 1;
        midpointCache[key] = index;
        return index;
    };

    std::vector<std::array<int, 3>> subdivided;
    subdivided.reserve(faces.size() * 4);
    for (const auto& face : faces) {
        int ab = midpoint(face[0], face[1]);
        int bc = midpoint(face[1], face[2]);
        int ca = midpoint(face[2], face[0]);
        subdivided.push_back({face[0], ab, ca});
        subdivided.push_back({ab, face[1], bc});
        subdivided.push_back({ca, bc, face[2]});
        subdivided.push_back({ab, bc, ca});
    }
    faces = std::move(subdivided);
}

// Appends a gently-irregular, once-subdivided icosphere blob centered at
// `center`, for tree leaf clusters and shrubs. Fractal displacement keeps
// the extra geometry organic instead of merely making a smoother sphere.
void appendLeafBlob(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec3 center,
                     float radius, glm::vec3 color, std::mt19937& rng) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::vector<glm::vec3> base = {
        glm::normalize(glm::vec3(-1, t, 0)), glm::normalize(glm::vec3(1, t, 0)),
        glm::normalize(glm::vec3(-1, -t, 0)), glm::normalize(glm::vec3(1, -t, 0)),
        glm::normalize(glm::vec3(0, -1, t)), glm::normalize(glm::vec3(0, 1, t)),
        glm::normalize(glm::vec3(0, -1, -t)), glm::normalize(glm::vec3(0, 1, -t)),
        glm::normalize(glm::vec3(t, 0, -1)), glm::normalize(glm::vec3(t, 0, 1)),
        glm::normalize(glm::vec3(-t, 0, -1)), glm::normalize(glm::vec3(-t, 0, 1)),
    };
    std::vector<std::array<int, 3>> faces = {
        {0, 11, 5}, {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9},  {5, 11, 4},
        {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},  {3, 2, 6},  {3, 6, 8},
        {3, 8, 9},  {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},  {9, 8, 1},
    };

    subdivideIcosphere(base, faces);

    std::uniform_real_distribution<float> offsetDist(0.0f, 1000.0f);
    glm::vec3 noiseOffset(offsetDist(rng), offsetDist(rng), offsetDist(rng));
    std::vector<glm::vec3> deformed(base.size());
    for (size_t i = 0; i < base.size(); ++i) {
        float displacement = 0.78f + fractalNoise3D(base[i] * 2.4f + noiseOffset, 4) * 0.42f;
        deformed[i] = base[i] * displacement * radius;
    }

    for (const auto& face : faces) {
        glm::vec3 dir0 = base[face[0]];
        glm::vec3 dir1 = base[face[1]];
        glm::vec3 dir2 = base[face[2]];
        glm::vec3 p0 = center + deformed[face[0]];
        glm::vec3 p1 = center + deformed[face[1]];
        glm::vec3 p2 = center + deformed[face[2]];

        glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        glm::vec3 centroid = (p0 + p1 + p2) / 3.0f - center;
        if (glm::dot(normal, centroid) < 0.0f) {
            std::swap(p1, p2);
            std::swap(dir1, dir2);
            normal = -normal;
        }

        auto sphericalUV = [](glm::vec3 direction) {
            glm::vec3 d = glm::normalize(direction);
            float u = std::atan2(d.z, d.x) / (2.0f * kPi) + 0.5f;
            float v = std::acos(glm::clamp(d.y, -1.0f, 1.0f)) / kPi;
            return glm::vec2(u, v) * 2.0f;
        };

        uint32_t base_ = static_cast<uint32_t>(vertices.size());
        vertices.push_back({p0, normal, color, sphericalUV(dir0)});
        vertices.push_back({p1, normal, color, sphericalUV(dir1)});
        vertices.push_back({p2, normal, color, sphericalUV(dir2)});
        indices.insert(indices.end(), {base_ + 0, base_ + 1, base_ + 2});
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

    // Standard per-face 0..1 UV unwrap (corner order -> (0,0),(1,0),(1,1),
    // (0,1)) -- doesn't correspond to any particular world axis per face,
    // but that's fine for a texture without a required orientation (see
    // CrateTextureGenerator); meshes that don't sample a real texture
    // (bound to the shared plain white texture) are unaffected either way.
    const glm::vec2 faceUVs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    for (const auto& face : faces) {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        for (int i = 0; i < 4; ++i) {
            vertices.push_back({face.corners[i], face.normal, color, faceUVs[i]});
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
    std::vector<glm::vec3> verts = {
        glm::normalize(glm::vec3(-1, t, 0)), glm::normalize(glm::vec3(1, t, 0)),
        glm::normalize(glm::vec3(-1, -t, 0)), glm::normalize(glm::vec3(1, -t, 0)),
        glm::normalize(glm::vec3(0, -1, t)), glm::normalize(glm::vec3(0, 1, t)),
        glm::normalize(glm::vec3(0, -1, -t)), glm::normalize(glm::vec3(0, 1, -t)),
        glm::normalize(glm::vec3(t, 0, -1)), glm::normalize(glm::vec3(t, 0, 1)),
        glm::normalize(glm::vec3(-t, 0, -1)), glm::normalize(glm::vec3(-t, 0, 1)),
    };
    std::vector<std::array<int, 3>> faces = {
        {0, 11, 5}, {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9},  {5, 11, 4},
        {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},  {3, 2, 6},  {3, 6, 8},
        {3, 8, 9},  {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},  {9, 8, 1},
    };

    // Subdivide twice (20 -> 80 -> 320 faces): split each triangle into 4
    // via shared edge midpoints so the multi-scale fractal
    // displacement below varies smoothly across it instead of tearing the
    // surface at the original icosahedron's edges. This alone is what
    // makes the noise below read as organic bumps rather than one uniform
    // wobble per original vertex.
    subdivideIcosphere(verts, faces);
    subdivideIcosphere(verts, faces);

    // Multi-octave ("fractal") radius displacement per unique vertex
    // direction -- large dents plus fine surface roughness layered
    // together, much more organic than a single random jitter per base
    // icosahedron vertex. Deterministic per seed so the same seed always
    // produces the same rock (used to build a small, reusable pool of
    // distinct-looking variants -- see Application::rockMeshes_).
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> offsetDist(0.0f, 1000.0f);
    glm::vec3 seedOffset(offsetDist(rng), offsetDist(rng), offsetDist(rng));
    std::uniform_real_distribution<float> colorJitter(-0.05f, 0.05f);

    std::vector<glm::vec3> deformed(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        float broad = fractalNoise3D(verts[i] * 1.35f + seedOffset, 5);
        float detail = fractalNoise3D(verts[i] * 4.5f + seedOffset * 1.73f, 4);
        float ridge = 1.0f - std::abs(detail * 2.0f - 1.0f);
        float radius = 0.62f + broad * 0.52f + ridge * broad * 0.2f;
        deformed[i] = verts[i] * radius;
        deformed[i].y *= 0.78f;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(faces.size() * 3);
    indices.reserve(faces.size() * 3);

    // Spherical UV, scaled to repeat the (already-tileable) rock texture a
    // few times across the rock's surface for close-up surface detail --
    // some pole pinching/seam is possible with this simple a mapping, but
    // unnoticeable on a small, already-irregular boulder.
    auto sphericalUV = [](glm::vec3 direction) {
        glm::vec3 d = glm::normalize(direction);
        float u = std::atan2(d.z, d.x) / (2.0f * kPi) + 0.5f;
        float v = std::acos(glm::clamp(d.y, -1.0f, 1.0f)) / kPi;
        return glm::vec2(u, v) * 3.0f;
    };

    for (const auto& face : faces) {
        glm::vec3 dir0 = verts[face[0]];
        glm::vec3 dir1 = verts[face[1]];
        glm::vec3 dir2 = verts[face[2]];
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
            std::swap(dir1, dir2);
            normal = -normal;
        }

        glm::vec3 color = glm::clamp(baseColor + glm::vec3(colorJitter(rng)), glm::vec3(0.0f),
                                      glm::vec3(1.0f));

        uint32_t base_ = static_cast<uint32_t>(vertices.size());
        vertices.push_back({p0, normal, color, sphericalUV(dir0)});
        vertices.push_back({p1, normal, color, sphericalUV(dir1)});
        vertices.push_back({p2, normal, color, sphericalUV(dir2)});
        indices.insert(indices.end(), {base_ + 0, base_ + 1, base_ + 2});
    }

    return Mesh(ctx, commands, vertices, indices);
}

Mesh Mesh::sedimentaryCliff(VulkanContext& ctx, CommandContext& commands, glm::vec3 baseColor,
                            uint32_t seed, bool topOnly) {
    std::mt19937 rng(seed);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> signedUnit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> colorDist(-0.08f, 0.08f);

    // One formation is a broken run of separate, partly overlapping stone
    // plates. That discontinuity is essential: a single extruded outline
    // reads as a manufactured platform even when its surface is noisy.
    auto appendPlate = [&](glm::vec3 center, float width, float depth, float thickness,
                           float angle, int corners) {
        std::vector<glm::vec3> bottom(corners), top(corners);
        float ca = std::cos(angle), sa = std::sin(angle);
        for (int i = 0; i < corners; ++i) {
            float theta = static_cast<float>(i) / corners * 2.0f * kPi;
            float radial = 0.78f + unit(rng) * 0.3f;
            float lx = std::cos(theta) * width * 0.5f * radial;
            float lz = std::sin(theta) * depth * 0.5f * radial;
            glm::vec3 offset(ca * lx + sa * lz, 0.0f, -sa * lx + ca * lz);
            // Extend the plate downward below its authored center. This
            // extra buried skirt prevents gaps on uneven terrain without
            // lowering the visible top of the outcrop.
            bottom[i] = center + offset - glm::vec3(0.0f, 0.3f, 0.0f);
            top[i] = center + offset +
                     glm::vec3(signedUnit(rng) * 0.025f, thickness, signedUnit(rng) * 0.025f);
        }

        glm::vec3 color = glm::clamp(baseColor + glm::vec3(colorDist(rng)), glm::vec3(0.0f),
                                      glm::vec3(1.0f));
        constexpr float kGrassTopOffset = 0.12f;
        float topOffset = topOnly ? kGrassTopOffset : 0.0f;
        glm::vec3 topCenter(0.0f);
        for (glm::vec3 p : top) topCenter += p + glm::vec3(0.0f, topOffset, 0.0f);
        topCenter /= static_cast<float>(corners);
        uint32_t centerIndex = static_cast<uint32_t>(vertices.size());
        vertices.push_back({topCenter, {0.0f, 1.0f, 0.0f}, color, {0.5f, 0.5f}});
        for (int i = 0; i < corners; ++i) {
            uint32_t index = static_cast<uint32_t>(vertices.size());
            glm::vec3 topA = top[i] + glm::vec3(0.0f, topOffset, 0.0f);
            glm::vec3 topB = top[(i + 1) % corners] + glm::vec3(0.0f, topOffset, 0.0f);
            vertices.push_back({topA, {0.0f, 1.0f, 0.0f}, color,
                                {top[i].x / width + 0.5f, top[i].z / depth + 0.5f}});
            vertices.push_back({topB, {0.0f, 1.0f, 0.0f}, color,
                                {top[(i + 1) % corners].x / width + 0.5f,
                                 top[(i + 1) % corners].z / depth + 0.5f}});
            indices.insert(indices.end(), {centerIndex, index + 1, index});

            glm::vec3 lowerA = topOnly ? top[i] : bottom[i];
            glm::vec3 lowerB = topOnly ? top[(i + 1) % corners] : bottom[(i + 1) % corners];
            glm::vec3 outward = (topA + topB) * 0.5f - center;
            glm::vec3 normal = glm::normalize(glm::cross(lowerB - lowerA, topB - lowerA));
            bool flipSide = glm::dot(normal, outward) < 0.0f;
            if (flipSide) normal = -normal;
            uint32_t side = static_cast<uint32_t>(vertices.size());
            vertices.push_back({lowerA, normal, color, {0.0f, 0.0f}});
            vertices.push_back({lowerB, normal, color, {1.0f, 0.0f}});
            vertices.push_back({topB, normal, color, {1.0f, 1.0f}});
            vertices.push_back({topA, normal, color, {0.0f, 1.0f}});
            if (!flipSide) {
                indices.insert(indices.end(), {side, side + 1, side + 2, side, side + 2, side + 3});
            } else {
                indices.insert(indices.end(), {side, side + 2, side + 1, side, side + 3, side + 2});
            }
        }
    };

    constexpr int kBasePlates = 15;
    for (int i = 0; i < kBasePlates; ++i) {
        float t = static_cast<float>(i) / (kBasePlates - 1);
        float x = (t - 0.5f) * 19.2f + signedUnit(rng) * 0.34f;
        // Broad, slow bends make the combined formation follow a natural
        // seam instead of forming one ruler-straight row of stones.
        float z = std::sin(t * kPi * 2.0f + seed * 0.17f) * 0.72f + signedUnit(rng) * 0.34f;
        float width = 1.92f + unit(rng) * 1.2f;
        float depth = 1.56f + unit(rng) * 0.92f;
        float thickness = 0.11f + unit(rng) * 0.09f;
        float angle = signedUnit(rng) * 0.24f;
        float baseY = unit(rng) * 0.07f;
        appendPlate({x, baseY, z}, width, depth, thickness, angle, 7 + static_cast<int>(unit(rng) * 3.0f));

        // Intermittent second courses suggest sedimentary bedding without
        // producing the uniform layer-cake silhouette of the old mesh.
        if (i > 0 && i < kBasePlates - 1 && unit(rng) < 0.72f) {
            appendPlate({x + signedUnit(rng) * 0.14f, baseY + thickness * 0.82f,
                         z + signedUnit(rng) * 0.1f},
                        width * (0.62f + unit(rng) * 0.18f), depth * 0.82f,
                        thickness * (0.65f + unit(rng) * 0.2f), angle + signedUnit(rng) * 0.12f,
                        7 + static_cast<int>(unit(rng) * 3.0f));
        }
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

Mesh Mesh::shell(VulkanContext& ctx, CommandContext& commands, glm::vec3 color) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    constexpr int kSides = 8;
    constexpr float kBodyRadius = 0.07f;
    constexpr float kBodyLength = 0.22f;
    constexpr float kNoseLength = 0.13f;
    // Not a literal 0 -- see appendOrientedFrustum's own comment on why a
    // near-pointed tip avoids degenerate zero-area triangles there.
    constexpr float kTipRadius = 0.01f;

    const glm::vec3 dir(0.0f, 0.0f, 1.0f);
    const glm::vec3 bodyBase(0.0f, 0.0f, -(kBodyLength + kNoseLength) * 0.5f);
    appendOrientedFrustum(vertices, indices, bodyBase, dir, kBodyLength, kBodyRadius, kBodyRadius,
                          color, kSides, 1.0f);
    glm::vec3 noseBase = bodyBase + dir * kBodyLength;
    appendOrientedFrustum(vertices, indices, noseBase, dir, kNoseLength, kBodyRadius, kTipRadius, color,
                          kSides, 1.0f);

    // Flat cap closing the body's open rear end -- a fan from a center
    // vertex to the same ring appendOrientedFrustum's first call already
    // built at bodyBase, using the same (right, fwd) = ((1,0,0), (0,1,0))
    // basis it derives internally for this exact dir so the cap's ring
    // matches the body's without recomputing/duplicating those vertices.
    // Winding verified defensively rather than reasoned out by hand, same
    // spirit as appendOrientedFrustum's own check.
    uint32_t centerIdx = static_cast<uint32_t>(vertices.size());
    vertices.push_back({bodyBase, -dir, color, glm::vec2(0.5f)});
    uint32_t ringStart = static_cast<uint32_t>(vertices.size());
    for (int i = 0; i <= kSides; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(kSides) * 2.0f * kPi;
        glm::vec3 ringDir = std::cos(angle) * glm::vec3(1.0f, 0.0f, 0.0f) +
                             std::sin(angle) * glm::vec3(0.0f, 1.0f, 0.0f);
        vertices.push_back({bodyBase + ringDir * kBodyRadius, -dir, color,
                             glm::vec2(0.5f) + 0.5f * glm::vec2(ringDir.x, ringDir.y)});
    }
    glm::vec3 c0 = vertices[centerIdx].position;
    glm::vec3 c1 = vertices[ringStart].position;
    glm::vec3 c2 = vertices[ringStart + 1].position;
    bool capFlip = glm::dot(glm::normalize(glm::cross(c1 - c0, c2 - c0)), -dir) < 0.0f;
    for (int i = 0; i < kSides; ++i) {
        uint32_t a = ringStart + i;
        uint32_t b = ringStart + i + 1;
        if (!capFlip) {
            indices.insert(indices.end(), {centerIdx, a, b});
        } else {
            indices.insert(indices.end(), {centerIdx, b, a});
        }
    }

    return Mesh(ctx, commands, vertices, indices);
}

Mesh Mesh::blobCluster(VulkanContext& ctx, CommandContext& commands, glm::vec3 color) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    // Fixed seed: this mesh's own irregularity doesn't need to vary --
    // per-instance variety comes from the caller randomizing each
    // instance's own position/scale/velocity instead (see SmokePuff.h and
    // Application::fireProjectile/spawnExplosion).
    std::mt19937 rng(1337);
    appendLeafBlob(vertices, indices, glm::vec3(0.0f), 0.5f, color, rng);
    appendLeafBlob(vertices, indices, glm::vec3(0.2f, 0.06f, 0.05f), 0.34f, color, rng);
    appendLeafBlob(vertices, indices, glm::vec3(-0.16f, -0.08f, -0.1f), 0.3f, color, rng);
    return Mesh(ctx, commands, vertices, indices);
}

Mesh Mesh::shrub(VulkanContext& ctx, CommandContext& commands, glm::vec3 color, uint32_t seed) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::mt19937 rng(seed);
    // Centers offset upward by roughly their own radius so the cluster's
    // underside sits near y=0 (ground level) instead of the whole thing
    // floating centered on it -- same "sits on the ground" reasoning as
    // DebrisParticle/RockInstance's own embed-depth handling elsewhere.
    appendLeafBlob(vertices, indices, glm::vec3(0.0f, 0.24f, 0.0f), 0.28f, color, rng);
    appendLeafBlob(vertices, indices, glm::vec3(0.18f, 0.17f, 0.08f), 0.2f, color, rng);
    appendLeafBlob(vertices, indices, glm::vec3(-0.15f, 0.15f, -0.1f), 0.18f, color, rng);
    return Mesh(ctx, commands, vertices, indices);
}

// Recursively builds one branch (as an oriented frustum) and, at the
// bottom of the recursion, a small leaf cluster -- the fractal structure
// that gives the tree its shape: each branch is a smaller, randomly
// reoriented copy of the same "segment then split" rule applied to its
// parent. Appends into whichever of barkV/leafV the caller cares about
// (see the treeBark/treeLeaves split below); the *other* is still fully
// walked so the RNG sequence -- and therefore the branch structure/leaf
// placement -- stays identical between the two calls for the same seed.
void buildTreeBranch(std::vector<Vertex>& barkV, std::vector<uint32_t>& barkI,
                      std::vector<Vertex>& leafV, std::vector<uint32_t>& leafI, std::mt19937& rng,
                      glm::vec3 base, glm::vec3 dir, float length, float radius, int depth,
                      glm::vec3 barkColor, glm::vec3 leafColor) {
    dir = glm::normalize(dir);
    float tipRadius = std::max(radius * 0.6f, 0.01f);
    int sides = depth >= 2 ? 10 : 8;
    appendOrientedFrustum(barkV, barkI, base, dir, length, radius, tipRadius, barkColor, sides, 1.2f);
    glm::vec3 tip = base + dir * length;

    if (depth <= 0) {
        // Leaf cluster: a big irregular blob at the tip plus a couple of
        // smaller ones clustered around it, instead of thin cones -- reads
        // as an actual mass of foliage rather than a pine-tree silhouette.
        float blobRadius = radius * 16.0f + 0.35f;
        appendLeafBlob(leafV, leafI, tip, blobRadius, leafColor, rng);
        std::uniform_real_distribution<float> smallOffsetDist(-blobRadius * 0.5f, blobRadius * 0.5f);
        for (int i = 0; i < 2; ++i) {
            glm::vec3 offset(smallOffsetDist(rng), smallOffsetDist(rng), smallOffsetDist(rng));
            appendLeafBlob(leafV, leafI, tip + offset, blobRadius * 0.65f, leafColor, rng);
        }
        return;
    }

    std::uniform_int_distribution<int> branchCountDist(2, 3);
    std::uniform_real_distribution<float> azimuthDist(0.0f, 2.0f * kPi);
    std::uniform_real_distribution<float> deviationDist(0.5f, 0.85f);  // radians, ~29-49 degrees

    glm::vec3 up = std::abs(dir.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 perp1 = glm::normalize(glm::cross(up, dir));
    glm::vec3 perp2 = glm::cross(dir, perp1);

    int branchCount = branchCountDist(rng);
    for (int i = 0; i < branchCount; ++i) {
        float azimuth = azimuthDist(rng);
        glm::vec3 axis = glm::normalize(std::cos(azimuth) * perp1 + std::sin(azimuth) * perp2);
        float deviation = deviationDist(rng);
        glm::vec3 newDir = glm::vec3(
            glm::rotate(glm::mat4(1.0f), deviation, axis) * glm::vec4(dir, 0.0f));
        buildTreeBranch(barkV, barkI, leafV, leafI, rng, tip, newDir, length * 0.68f, radius * 0.62f,
                         depth - 1, barkColor, leafColor);
    }
}

Mesh Mesh::treeBark(VulkanContext& ctx, CommandContext& commands, glm::vec3 tint, uint32_t seed) {
    std::vector<Vertex> barkV, leafV;
    std::vector<uint32_t> barkI, leafI;
    std::mt19937 rng(seed);
    buildTreeBranch(barkV, barkI, leafV, leafI, rng, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 1.1f,
                     0.14f, 3, tint, glm::vec3(0.0f));
    return Mesh(ctx, commands, barkV, barkI);
}

Mesh Mesh::treeLeaves(VulkanContext& ctx, CommandContext& commands, glm::vec3 tint, uint32_t seed) {
    std::vector<Vertex> barkV, leafV;
    std::vector<uint32_t> barkI, leafI;
    std::mt19937 rng(seed);
    buildTreeBranch(barkV, barkI, leafV, leafI, rng, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 1.1f,
                     0.14f, 3, glm::vec3(0.0f), tint);
    return Mesh(ctx, commands, leafV, leafI);
}
