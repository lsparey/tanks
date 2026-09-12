#include "Mesh.h"
#include "../scene/FoliageLod.h"
#include "DrawStatistics.h"
#include "VoxelSurface.h"
#include "../scene/TreeGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

// Appends a gently-irregular icosphere blob centered at `center`, for tree
// leaf clusters and shrubs. The default once-subdivided form has 80 faces;
// far tree LODs request the base 20-face form. Fractal displacement keeps
// either version organic instead of merely making a smoother sphere.
//
// clusterTop > 0 enables a vertical albedo gradient across the whole
// cluster the blob belongs to: vertex color scales from shadeLow at y=0
// (ground) up to shadeHigh at y=clusterTop. A cheap baked stand-in for the
// self-shadowing/inner-canopy darkening a real bush has -- without it a
// multi-blob shrub reads as uniformly-lit green balls.
void appendLeafBlob(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec3 center,
                     float radius, glm::vec3 color, std::mt19937& rng, int subdivisions = 1,
                     float clusterTop = 0.0f, float shadeLow = 0.6f, float shadeHigh = 1.15f) {
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

    for (int level = 0; level < subdivisions; ++level) subdivideIcosphere(base, faces);

    std::uniform_real_distribution<float> offsetDist(0.0f, 1000.0f);
    glm::vec3 noiseOffset(offsetDist(rng), offsetDist(rng), offsetDist(rng));
    std::vector<glm::vec3> deformed(base.size());
    for (size_t i = 0; i < base.size(); ++i) {
        float displacement = 0.78f + fractalNoise3D(base[i] * 2.4f + noiseOffset, 4) * 0.42f;
        deformed[i] = base[i] * displacement * radius;
    }

    // Average the surrounding face normals at every shared source vertex.
    // The render vertices below are still duplicated per triangle (which
    // keeps the simple spherical UV seam intact), but sharing these normals
    // makes lighting flow continuously over the foliage blob instead of
    // revealing every triangle as a separate flat-shaded polygon.
    std::vector<glm::vec3> smoothNormals(base.size(), glm::vec3(0.0f));
    for (const auto& face : faces) {
        const glm::vec3& p0 = deformed[face[0]];
        const glm::vec3& p1 = deformed[face[1]];
        const glm::vec3& p2 = deformed[face[2]];
        glm::vec3 weightedNormal = glm::cross(p1 - p0, p2 - p0);
        glm::vec3 centroid = (p0 + p1 + p2) / 3.0f;
        if (glm::dot(weightedNormal, centroid) < 0.0f) weightedNormal = -weightedNormal;
        smoothNormals[face[0]] += weightedNormal;
        smoothNormals[face[1]] += weightedNormal;
        smoothNormals[face[2]] += weightedNormal;
    }
    for (glm::vec3& normal : smoothNormals) normal = glm::normalize(normal);

    for (const auto& face : faces) {
        glm::vec3 dir0 = base[face[0]];
        glm::vec3 dir1 = base[face[1]];
        glm::vec3 dir2 = base[face[2]];
        glm::vec3 p0 = center + deformed[face[0]];
        glm::vec3 p1 = center + deformed[face[1]];
        glm::vec3 p2 = center + deformed[face[2]];
        glm::vec3 n0 = smoothNormals[face[0]];
        glm::vec3 n1 = smoothNormals[face[1]];
        glm::vec3 n2 = smoothNormals[face[2]];

        glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        glm::vec3 centroid = (p0 + p1 + p2) / 3.0f - center;
        if (glm::dot(normal, centroid) < 0.0f) {
            std::swap(p1, p2);
            std::swap(dir1, dir2);
            std::swap(n1, n2);
            normal = -normal;
        }

        auto sphericalUV = [](glm::vec3 direction) {
            glm::vec3 d = glm::normalize(direction);
            float u = std::atan2(d.z, d.x) / (2.0f * kPi) + 0.5f;
            float v = std::acos(glm::clamp(d.y, -1.0f, 1.0f)) / kPi;
            return glm::vec2(u, v) * 2.0f;
        };

        auto shaded = [&](const glm::vec3& position) {
            if (clusterTop <= 0.0f) return color;
            float t = glm::clamp(position.y / clusterTop, 0.0f, 1.0f);
            return color * glm::mix(shadeLow, shadeHigh, t);
        };

        uint32_t base_ = static_cast<uint32_t>(vertices.size());
        vertices.push_back({p0, n0, shaded(p0), sphericalUV(dir0)});
        vertices.push_back({p1, n1, shaded(p1), sphericalUV(dir1)});
        vertices.push_back({p2, n2, shaded(p2), sphericalUV(dir2)});
        indices.insert(indices.end(), {base_ + 0, base_ + 1, base_ + 2});
    }
}

// Trees use a voxel occupancy field with a rounded boundary mesh.
using VoxelSet = VoxelSurface::Cells;

// Marks every voxel whose center lies within a tapered capsule from `a`
// (radius radiusA) to `b` (radius radiusB) -- used for branch segments.
// Tests each candidate cell directly against the segment (closest-point
// distance) rather than stepping along it, so the result doesn't depend on
// step size and adjoining branch segments connect cleanly with no seam at
// the joint, unlike the flat cross-section where the old frustums met.
void voxelizeCapsule(VoxelSet& voxels, glm::vec3 a, glm::vec3 b, float radiusA, float radiusB,
                     float voxelSize, glm::vec3 noiseOffset) {
    // Bark noise: a perfectly round, linearly-tapered capsule reads as a
    // smooth pipe even at fine voxel resolution. Perturbing the radius by
    // the branch's own 3D noise field (rather than anything a-priori
    // circular) adds knots/ridges/bumps that break that up into something
    // closer to real bark -- same fractalNoise3D idiom rock's displacement
    // uses, just applied to a branch radius instead of a boulder's.
    float maxRadius = std::max(radiusA, radiusB) * 1.35f;
    glm::vec3 lo = glm::min(a, b) - glm::vec3(maxRadius);
    glm::vec3 hi = glm::max(a, b) + glm::vec3(maxRadius);
    glm::ivec3 loCell = glm::ivec3(glm::floor(lo / voxelSize));
    glm::ivec3 hiCell = glm::ivec3(glm::floor(hi / voxelSize));
    glm::vec3 ab = b - a;
    float abLen2 = glm::dot(ab, ab);
    for (int z = loCell.z; z <= hiCell.z; ++z) {
        for (int y = loCell.y; y <= hiCell.y; ++y) {
            for (int x = loCell.x; x <= hiCell.x; ++x) {
                if (voxels.contains({x, y, z})) continue;
                glm::vec3 p = (glm::vec3(x, y, z) + 0.5f) * voxelSize;
                float t = abLen2 > 1e-8f ? glm::clamp(glm::dot(p - a, ab) / abLen2, 0.0f, 1.0f) : 0.0f;
                glm::vec3 closest = a + ab * t;
                float r = glm::mix(radiusA, radiusB, t);
                float dist = glm::length(p - closest);
                // Noise lies in [0, 1], so only the boundary shell needs it.
                if (dist > r * 1.31f) continue;
                if (dist < r * 0.69f) {
                    voxels.insert({x, y, z});
                    continue;
                }
                float bark = 1.0f + (fractalNoise3D(p * 9.0f + noiseOffset, 3) - 0.5f) * 0.6f;
                if (dist <= r * bark) voxels.insert({x, y, z});
            }
        }
    }
}

// A thin, oriented leaf spray with a ragged outline. Shape is evaluated in
// its own axes so pine needles and broadleaf fans never inflate into balls.
constexpr float kNearLeafVoxelSize = .036f;
void voxelizeLeafSpray(VoxelSet& voxels, const TreeGenerator::Spray& spray, float voxelSize) {
    glm::vec3 radii = spray.radii;
    // Keep thin foliage represented on coarse grids without widening the
    // spray or filling the larger gaps between branches.
    radii.z = std::max(radii.z, voxelSize * .75f);
    glm::vec3 extent = (glm::abs(spray.axes[0]) * radii.x + glm::abs(spray.axes[1]) * radii.y
                        + glm::abs(spray.axes[2]) * radii.z) * 1.08f;
    glm::ivec3 lo = glm::ivec3(glm::floor((spray.center - extent) / voxelSize));
    glm::ivec3 hi = glm::ivec3(glm::floor((spray.center + extent) / voxelSize));
    glm::mat3 inverseAxes = glm::transpose(spray.axes);
    glm::vec3 noiseOffset(float(spray.seed & 1023u), float((spray.seed >> 10) & 1023u),
                          float((spray.seed >> 20) & 1023u));
    for (int z = lo.z; z <= hi.z; ++z) for (int y = lo.y; y <= hi.y; ++y)
        for (int x = lo.x; x <= hi.x; ++x) {
            glm::vec3 p = (glm::vec3(x,y,z) + .5f) * voxelSize;
            glm::vec3 local = (inverseAxes * (p - spray.center)) / radii;
            float distanceSquared = glm::dot(local, local);
            if (distanceSquared > 1.07f * 1.07f) continue;
            if (distanceSquared < .77f * .77f) { voxels.insert({x,y,z}); continue; }
            // Most of an oriented spray's AABB is empty: reject that space
            // before hashing. Only the boundary needs noise or a square root.
            if (voxels.contains({x,y,z})) continue;
            float distance = std::sqrt(distanceSquared);
            glm::vec3 direction = local / distance;
            float noise = fractalNoise3D(direction * 3.4f + noiseOffset, 3);
            float lobes = std::abs(std::sin(std::atan2(direction.y, direction.x) * 5.0f + noiseOffset.x));
            if (distance <= .78f + noise * .20f + lobes * .08f) voxels.insert({x,y,z});
        }
}

// Oak laminae remain separate thin, closed surfaces instead of unioning
// into a smooth voxel mass. No alpha cutouts or special lighting path are
// needed; both faces have outward normals for the existing leaf material.
void appendOakLamina(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                   const TreeGenerator::Spray& leaf, glm::vec3 tint, int lod) {
    const bool proxy = lod == 3;
    int segments = proxy ? 4 : (lod == 0 ? 12 : (lod == 1 ? 6 : 4));
    glm::vec3 radii = leaf.radii * (proxy ? glm::vec3(.4f, .4f, .3f) : glm::vec3(1));
    float variation = .94f + float(leaf.seed & 255u) * (.12f / 255.f);
    for (int sign : {1, -1}) {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        vertices.push_back({leaf.center + leaf.axes[2] * (radii.z * sign),
                            leaf.axes[2] * float(sign), tint * variation, glm::vec2(.5f)});
        for (int j = 0; j < segments; ++j) {
            float angle = 2 * kPi * j / segments;
            float x = std::cos(angle);
            float y = std::sin(angle) * (proxy ? 1.f : .82f + .18f * std::cos(6 * angle));
            glm::vec3 normal = glm::normalize(leaf.axes * glm::vec3(x * .06f, y * .12f, float(sign)));
            vertices.push_back({leaf.center + leaf.axes * (glm::vec3(x,y,0) * radii),
                                normal, tint * variation, glm::vec2(x,y) * .5f + .5f});
        }
        for (int j = 0; j < segments; ++j) {
            uint32_t a = base + 1 + j, b = base + 1 + (j + 1) % segments;
            if (sign < 0) std::swap(a, b);
            indices.insert(indices.end(), {base, a, b});
        }
    }
}

void appendOakLeaf(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                   const TreeGenerator::Spray& spray, glm::vec3 tint, int lod) {
    // Fine leaves overlap in projection without sharing their edge normals.
    // Coarser LODs use their envelope once individual edges are sub-pixel.
    if (lod == 1 || lod == 2) { appendOakLamina(vertices, indices, spray, tint, lod); return; }
    constexpr glm::vec3 offsets[] = {{-.43f,-.35f,-.3f}, {.0f,.42f,.3f}, {.43f,-.3f,0}};
    for (int j=0;j<3;++j) {
        auto leaf = spray;
        leaf.center += spray.axes * (offsets[j] * spray.radii);
        leaf.radii *= glm::vec3(.55f,.62f,.7f);
        leaf.seed += j * 7919u;
        appendOakLamina(vertices, indices, leaf, tint, lod);
    }
}

// Tiny sprays can be only a few cells thick. Fit their shadow proxy to
// their actual near-grid occupancy rather than assuming a fixed inset is
// safe for every sub-voxel offset and orientation.
void appendLeafSprayProxy(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                           const TreeGenerator::Spray& spray, glm::vec3 tint) {
    VoxelSet cells;
    voxelizeLeafSpray(cells, spray, kNearLeafVoxelSize);
    // Eight triangles are sufficient for a tiny transmissive occluder.
    // Keeping all sprays, with simpler proxies, preserves crown coverage.
    std::array<Vertex, 6> unit{};
    constexpr glm::vec3 tips[] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
    constexpr uint32_t faces[] = {4,0,2, 4,2,1, 4,1,3, 4,3,0,
                                  5,2,0, 5,1,2, 5,3,1, 5,0,3};
    for (size_t i = 0; i < unit.size(); ++i) unit[i] = {tips[i], tips[i], tint, glm::vec2(0)};
    auto inside = [&](glm::vec3 p) { return cells.contains(glm::ivec3(glm::floor(p / kNearLeafVoxelSize))); };

    // Pine/ash sprays are flattened, not spherical (radii like .29/.12/.07):
    // a single inset shared by all six tips is bottlenecked by the thinnest
    // axis, so the long axis was left well inside the occupied voxels for no
    // reason. Fit each tip's reach independently, then only pull back
    // whichever tip currently reaches furthest on a face that pokes outside.
    constexpr float kInsets[] = {.6f, .5f, .45f, .4f, .35f, .3f, .25f, .2f, .15f};
    constexpr int kInsetCount = static_cast<int>(std::size(kInsets));
    auto positionAt = [&](size_t tip, int level) {
        return spray.center + spray.axes * (unit[tip].position * spray.radii * kInsets[level]);
    };
    std::array<int, 6> tipLevel{};
    for (size_t i = 0; i < unit.size(); ++i) {
        tipLevel[i] = -1;
        for (int level = 0; level < kInsetCount; ++level) {
            if (inside(positionAt(i, level))) { tipLevel[i] = level; break; }
        }
        // A marginal spray still renders, but casts no independent proxy if
        // the rounded grid cannot safely contain one at this resolution.
        if (tipLevel[i] < 0) return;
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t f = 0; f < std::size(faces); f += 3) {
            uint32_t a = faces[f], b = faces[f + 1], c = faces[f + 2];
            glm::vec3 center = (positionAt(a, tipLevel[a]) + positionAt(b, tipLevel[b])
                               + positionAt(c, tipLevel[c])) / 3.0f;
            if (inside(center)) continue;
            uint32_t deepest = a;
            if (tipLevel[b] < tipLevel[deepest]) deepest = b;
            if (tipLevel[c] < tipLevel[deepest]) deepest = c;
            if (tipLevel[deepest] + 1 >= kInsetCount) return;
            ++tipLevel[deepest];
            changed = true;
        }
    }
    uint32_t base = static_cast<uint32_t>(vertices.size());
    for (size_t i = 0; i < unit.size(); ++i) {
        Vertex vertex = unit[i];
        vertex.position = positionAt(i, tipLevel[i]);
        vertex.normal = glm::normalize(spray.axes * (vertex.normal / spray.radii));
        vertices.push_back(vertex);
    }
    for (uint32_t index : faces) indices.push_back(base + index);
}

float calculateHorizontalInscribedRadius(const std::vector<glm::vec3>& positions) {
    // Find the smallest support distance of the XZ projection from the
    // local origin. For the convex, roughly round rock meshes this produces
    // a centered circle that stays within their projected silhouette.
    constexpr int kFootprintDirections = 64;
    float radius = std::numeric_limits<float>::max();
    for (int i = 0; i < kFootprintDirections; ++i) {
        float angle = static_cast<float>(i) / kFootprintDirections * 2.0f * kPi;
        glm::vec2 direction(std::cos(angle), std::sin(angle));
        float support = -std::numeric_limits<float>::max();
        for (const glm::vec3& position : positions) {
            support = std::max(support,
                               glm::dot(glm::vec2(position.x, position.z), direction));
        }
        radius = std::min(radius, support);
    }
    return positions.empty() ? 0.0f : std::max(radius, 0.0f);
}

}  // namespace

Mesh::Mesh(VulkanContext& ctx, CommandContext& commands, const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices, float horizontalInscribedRadius)
    : vertexBuffer_(Buffer::uploadDeviceLocal(ctx, commands, vertices.data(),
                                               sizeof(Vertex) * vertices.size(),
                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)),
      indexBuffer_(Buffer::uploadDeviceLocal(ctx, commands, indices.data(),
                                              sizeof(uint32_t) * indices.size(),
                                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT)),
      vertexCount_(static_cast<uint32_t>(vertices.size())),
      indexCount_(static_cast<uint32_t>(indices.size())),
      horizontalInscribedRadius_(horizontalInscribedRadius) {
    double radius = 0;
    for (const auto& vertex : vertices)
        radius = std::max(radius, std::hypot(double(vertex.position.x), double(vertex.position.z)));
    horizontalBoundingRadius_ = std::nextafter(float(radius), std::numeric_limits<float>::infinity());
}

void Mesh::bindAndDraw(VkCommandBuffer cmd) const {
    VkBuffer buffers[] = {vertexBuffer_.handle()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
    ++DrawStatistics::calls;
}

void Mesh::bindAndDrawInstanced(VkCommandBuffer cmd, uint32_t instanceCount,
                                uint32_t firstInstance) const {
    VkBuffer buffers[] = {vertexBuffer_.handle()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount_, instanceCount, 0, 0, firstInstance);
    ++DrawStatistics::calls;
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

Mesh Mesh::rock(VulkanContext& ctx, CommandContext& commands, glm::vec3 baseColor, uint32_t seed,
                int subdivisions, float radiusScale, float angularity) {
    angularity = glm::clamp(angularity, 0.0f, 1.0f);
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

    // Three subdivisions (20 -> 80 -> 320 -> 1280 faces) provide enough
    // vertices for small chips and ridges to affect the silhouette rather
    // than existing only in the material normal. The resulting meshes are
    // still tiny compared with the terrain and are reused by every rock
    // instance, so this doesn't multiply geometry by the instance count.
    subdivisions = std::clamp(subdivisions, 0, 3);
    for (int level = 0; level < subdivisions; ++level) subdivideIcosphere(verts, faces);

    // Multi-octave ("fractal") radius displacement per unique vertex
    // direction -- large dents plus fine surface roughness layered
    // together, much more organic than a single random jitter per base
    // icosahedron vertex. Deterministic per seed so the same seed always
    // produces the same rock (used to build a small, reusable pool of
    // distinct-looking variants -- see Application::rockMeshes_).
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> offsetDist(0.0f, 1000.0f);
    glm::vec3 seedOffset(offsetDist(rng), offsetDist(rng), offsetDist(rng));
    std::uniform_real_distribution<float> proportionDist(0.84f, 1.16f);
    glm::vec3 proportions(proportionDist(rng), proportionDist(rng), proportionDist(rng));
    // Angular scree is a flatter slab: squash the vertical proportion and
    // stretch one horizontal axis so it reads as a broken plate, not an egg.
    proportions.x *= 1.0f + 0.18f * angularity;
    proportions.y *= 1.0f - 0.42f * angularity;

    std::vector<glm::vec3> deformed(verts.size());
    // Broad displacement per vertex, kept for the crevice shading below:
    // low-displacement vertices are the recesses between lobes, exactly
    // where dirt/shadow accumulates on a real boulder.
    std::vector<float> relief(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        float broad = fractalNoise3D(verts[i] * 1.35f + seedOffset, 5);
        // Angular rocks fold the broad field into a ridged (creased) version:
        // the crossings become sharp fracture lines in the silhouette.
        broad = std::lerp(broad, 1.0f - std::abs(broad * 2.0f - 1.0f), angularity * 0.7f);
        float detail = fractalNoise3D(verts[i] * 4.5f + seedOffset * 1.73f, 4);
        float chips = fractalNoise3D(verts[i] * 11.0f + seedOffset * 2.41f, 3);
        float ridge = 1.0f - std::abs(detail * 2.0f - 1.0f);
        // A thresholded high-frequency field cuts localized shallow chips
        // into the surface. The ridge term adds raised fracture lines, and
        // a broad directional lobe stops the underlying form reading as a
        // uniformly noisy sphere. Fresh scree chips more and lobes harder.
        float chippedDepression = glm::smoothstep(0.68f - 0.1f * angularity, 0.82f, chips) *
                                  (0.13f + 0.05f * angularity);
        float directionalLobe = std::sin(verts[i].x * 3.7f + seedOffset.x) *
                                std::sin(verts[i].z * 2.9f + seedOffset.z) *
                                (0.055f * (1.0f + angularity));
        float radius = 0.62f + broad * 0.48f + ridge * broad * 0.18f + directionalLobe -
                       chippedDepression;
        relief[i] = glm::clamp(broad, 0.0f, 1.0f);
        deformed[i] = verts[i] * radius * proportions * radiusScale;
        deformed[i].y *= 0.78f;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(faces.size() * 3);
    indices.reserve(faces.size() * 3);

    // Accumulate area-weighted face normals on the shared, deformed source
    // mesh. Render vertices remain duplicated below for the spherical UV
    // seam, but reuse these averaged normals so illumination is continuous
    // across triangle boundaries. Geometric chips remain visible in the
    // silhouette and through the rock material's fine normal detail.
    std::vector<glm::vec3> smoothNormals(verts.size(), glm::vec3(0.0f));
    for (const auto& face : faces) {
        const glm::vec3& p0 = deformed[face[0]];
        const glm::vec3& p1 = deformed[face[1]];
        const glm::vec3& p2 = deformed[face[2]];
        glm::vec3 weightedNormal = glm::cross(p1 - p0, p2 - p0);
        glm::vec3 centroid = (p0 + p1 + p2) / 3.0f;
        if (glm::dot(weightedNormal, centroid) < 0.0f) weightedNormal = -weightedNormal;
        smoothNormals[face[0]] += weightedNormal;
        smoothNormals[face[1]] += weightedNormal;
        smoothNormals[face[2]] += weightedNormal;
    }
    for (glm::vec3& normal : smoothNormals) normal = glm::normalize(normal);

    // Per-vertex albedo: crevice darkening from the broad displacement
    // (recesses read as dirt/shadow), plus seed-varied moss/lichen patches
    // on upward faces -- some rocks stay bare, others get a distinctly
    // weathered top. Vertex color multiplies the gravel texture, so these
    // are tone shifts over its detail rather than flat paint.
    std::uniform_real_distribution<float> mossDist(-0.2f, 0.65f);
    const float mossStrength = std::max(0.0f, mossDist(rng));
    std::vector<glm::vec3> vertexColors(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        // Shading floor well above the texture's own dark bias -- the gravel
        // texture is deliberately dark for terrain use (it multiplies by 0.6
        // internally), and stacking a strong vertex darkening on top made
        // standalone boulders read as charcoal lumps against sunlit grass.
        glm::vec3 shaded = baseColor * (1.05f + relief[i] * 0.55f);
        float patch = fractalNoise3D(verts[i] * 3.3f + seedOffset * 2.9f, 3);
        float moss = glm::smoothstep(0.45f, 0.75f, patch) *
                     glm::smoothstep(0.15f, 0.65f, smoothNormals[i].y) * mossStrength;
        // Olive-green over the grey gravel texture reads as moss/lichen.
        vertexColors[i] = glm::mix(shaded, glm::vec3(0.5f, 0.72f, 0.28f), moss);
    }

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
        glm::vec3 n0 = smoothNormals[face[0]];
        glm::vec3 n1 = smoothNormals[face[1]];
        glm::vec3 n2 = smoothNormals[face[2]];

        // The icosahedron's own face winding isn't verified against this
        // project's CCW-outward convention, so derive it from the actual
        // (now-deformed) geometry instead of trusting the table: compute
        // the normal, and flip both it and the winding if it points inward.
        glm::vec3 c0 = vertexColors[face[0]];
        glm::vec3 c1 = vertexColors[face[1]];
        glm::vec3 c2 = vertexColors[face[2]];
        glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        glm::vec3 centroid = (p0 + p1 + p2) / 3.0f;
        if (glm::dot(normal, centroid) < 0.0f) {
            std::swap(p1, p2);
            std::swap(dir1, dir2);
            std::swap(n1, n2);
            std::swap(c1, c2);
            normal = -normal;
        }

        // Chisel: pull the smooth per-vertex normals partway back toward the
        // flat face normal. Fully smooth normals made every boulder read as
        // a soft grey lump; a partial blend keeps lighting continuous across
        // the big lobes while letting individual facets catch the light like
        // fracture planes.
        constexpr float kChisel = 0.4f;
        n0 = glm::normalize(glm::mix(n0, normal, kChisel));
        n1 = glm::normalize(glm::mix(n1, normal, kChisel));
        n2 = glm::normalize(glm::mix(n2, normal, kChisel));

        uint32_t base_ = static_cast<uint32_t>(vertices.size());
        vertices.push_back({p0, n0, c0, sphericalUV(dir0)});
        vertices.push_back({p1, n1, c1, sphericalUV(dir1)});
        vertices.push_back({p2, n2, c2, sphericalUV(dir2)});
        indices.insert(indices.end(), {base_ + 0, base_ + 1, base_ + 2});
    }

    return Mesh(ctx, commands, vertices, indices, calculateHorizontalInscribedRadius(deformed));
}

Mesh Mesh::dome(VulkanContext& ctx, CommandContext& commands, glm::vec3 color, float uvScale) {
    constexpr int kLatSegments = 12;
    constexpr int kLonSegments = 24;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(kLatSegments + 1) * (kLonSegments + 1));

    for (int lat = 0; lat <= kLatSegments; ++lat) {
        // Full sky sphere also covers below-horizon views from hilltops.
        float theta = (static_cast<float>(lat) / kLatSegments) * kPi;
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

Mesh Mesh::shard(VulkanContext& ctx, CommandContext& commands, glm::vec3 exteriorColor,
                 glm::vec3 fractureColor, uint32_t seed, glm::vec3 proportions) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    const std::array<glm::vec3, 12> baseVerts = {
        glm::normalize(glm::vec3(-1, t, 0)), glm::normalize(glm::vec3(1, t, 0)),
        glm::normalize(glm::vec3(-1, -t, 0)), glm::normalize(glm::vec3(1, -t, 0)),
        glm::normalize(glm::vec3(0, -1, t)), glm::normalize(glm::vec3(0, 1, t)),
        glm::normalize(glm::vec3(0, -1, -t)), glm::normalize(glm::vec3(0, 1, -t)),
        glm::normalize(glm::vec3(t, 0, -1)), glm::normalize(glm::vec3(t, 0, 1)),
        glm::normalize(glm::vec3(-t, 0, -1)), glm::normalize(glm::vec3(-t, 0, 1)),
    };
    const std::array<std::array<int, 3>, 20> faces = {{
        {0, 11, 5}, {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9},  {5, 11, 4},
        {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},  {3, 2, 6},  {3, 6, 8},
        {3, 8, 9},  {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},  {9, 8, 1},
    }};

    std::mt19937 rng(seed);
    // Wide radial jitter (down to half radius) is what breaks the recognizable
    // icosahedron silhouette into an irregular broken fragment.
    std::uniform_real_distribution<float> radiusDist(0.5f, 1.0f);
    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    std::array<glm::vec3, 12> deformed;
    for (size_t i = 0; i < baseVerts.size(); ++i) {
        deformed[i] = baseVerts[i] * radiusDist(rng) * proportions * 0.5f;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(faces.size() * 3);
    indices.reserve(faces.size() * 3);
    for (const auto& face : faces) {
        glm::vec3 p0 = deformed[face[0]];
        glm::vec3 p1 = deformed[face[1]];
        glm::vec3 p2 = deformed[face[2]];
        // Same derive-winding-from-geometry approach as Mesh::rock: flip if
        // the flat normal points inward.
        glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        if (glm::dot(normal, (p0 + p1 + p2) / 3.0f) < 0.0f) {
            std::swap(p1, p2);
            normal = -normal;
        }
        // Roughly half the facets read as fresh break; brightness jitter per
        // facet keeps even same-palette neighbors from merging visually.
        glm::vec3 faceColor = (roll(rng) < 0.45f ? fractureColor : exteriorColor) *
                              (0.85f + roll(rng) * 0.3f);
        uint32_t base = static_cast<uint32_t>(vertices.size());
        vertices.push_back({p0, normal, faceColor, glm::vec2(0.0f)});
        vertices.push_back({p1, normal, faceColor, glm::vec2(0.0f)});
        vertices.push_back({p2, normal, faceColor, glm::vec2(0.0f)});
        indices.insert(indices.end(), {base, base + 1, base + 2});
    }
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
    //
    // Five lobes (was three near-symmetric ones), each with rng-jittered
    // placement and its own slight green-tone shift, plus the vertical
    // shading gradient appendLeafBlob now bakes -- together these break the
    // old "three uniform green spheres" read into an irregular, darker-
    // bellied bush whose top catches the light.
    struct Lobe {
        glm::vec3 center;
        float radius;
    };
    const Lobe lobes[] = {
        {{0.0f, 0.26f, 0.0f}, 0.30f},   {{0.20f, 0.18f, 0.09f}, 0.21f},
        {{-0.17f, 0.16f, -0.11f}, 0.19f}, {{0.06f, 0.14f, -0.18f}, 0.16f},
        {{-0.09f, 0.13f, 0.16f}, 0.15f},
    };
    std::uniform_real_distribution<float> jitter(-0.05f, 0.05f);
    std::uniform_real_distribution<float> toneDist(0.84f, 1.1f);
    std::uniform_real_distribution<float> hueDist(-0.07f, 0.07f);
    const float clusterTop = 0.62f;
    for (const Lobe& lobe : lobes) {
        glm::vec3 center =
            lobe.center + glm::vec3(jitter(rng), jitter(rng) * 0.5f, jitter(rng));
        // Per-lobe tone: brightness plus a small warm/cool green shift, so
        // adjacent lobes read as different foliage depth, not copies.
        float hue = hueDist(rng);
        glm::vec3 lobeColor =
            glm::clamp(color * toneDist(rng) * glm::vec3(1.0f + hue, 1.0f, 1.0f - hue),
                       glm::vec3(0.0f), glm::vec3(1.25f));
        appendLeafBlob(vertices, indices, center, lobe.radius, lobeColor, rng,
                       /*subdivisions=*/1, clusterTop, /*shadeLow=*/0.5f, /*shadeHigh=*/1.18f);
    }
    return Mesh(ctx, commands, vertices, indices);
}

// Appends a single, gently bent grass blade -- a tapered triangle (wide at
// the base in the local XZ plane, y=0, narrowing to a point at the tip)
// baked with both triangle windings (see Mesh::dome's identical comment)
// so it reads from any side despite the main pipeline's fixed backface
// culling. `lean` offsets the tip sideways in XZ for a slightly bent look
// rather than a rigid upright spike.
void appendGrassBlade(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec2 base,
                     float height, float width, glm::vec2 lean, float yaw, glm::vec3 color) {
    float c = std::cos(yaw), s = std::sin(yaw);
    glm::vec2 halfWidth(c * width * 0.5f, s * width * 0.5f);
    glm::vec3 p0(base.x - halfWidth.x, 0.0f, base.y - halfWidth.y);
    glm::vec3 p1(base.x + halfWidth.x, 0.0f, base.y + halfWidth.y);
    glm::vec3 tip(base.x + lean.x, height, base.y + lean.y);
    glm::vec3 flatNormal = glm::normalize(glm::cross(p1 - p0, tip - p0));
    // Blended most of the way toward world-up rather than left as the raw
    // flat (mostly-sideways) face normal. A real tuft's blades scatter and
    // inter-reflect light between each other enough to read as evenly lit
    // from most angles; a single flat per-blade normal without this fake
    // left roughly half of any given tuft (whichever blades happened to
    // face away from the sun) reading as a dark, near-silhouette spike
    // against the sunlit lawn around it -- confirmed directly by sampling
    // rendered pixel colors on a tuft, which came back a uniform dark green
    // regardless of which way individual blades faced. This is the same
    // "fake it toward up" idea real-time grass shading commonly leans on in
    // place of simulating actual inter-blade light bounce.
    glm::vec3 normal = glm::normalize(glm::mix(flatNormal, glm::vec3(0.0f, 1.0f, 0.0f), 0.55f));

    uint32_t i0 = static_cast<uint32_t>(vertices.size());
    vertices.push_back({p0, normal, color, glm::vec2(0.0f, 0.0f)});
    vertices.push_back({p1, normal, color, glm::vec2(1.0f, 0.0f)});
    vertices.push_back({tip, normal, color, glm::vec2(0.5f, 1.0f)});
    indices.insert(indices.end(), {i0, i0 + 1, i0 + 2, i0, i0 + 2, i0 + 1});
}

// A small ground-level tuft of a handful of gently-leaning blades around a
// shared base point -- the near-field ground vegetation's actual geometry
// (real triangles, not an alpha-cutout billboard card: there's no existing
// alpha-cutout foliage texture/pipeline path in this codebase to reuse --
// see PLAN.md's "Near-field ground vegetation" for that tradeoff). `seed`
// varies blade count/placement/lean per variant, the same way Mesh::shrub's
// seed does for its blob jitter.
Mesh Mesh::grassClump(VulkanContext& ctx, CommandContext& commands, glm::vec3 color, uint32_t seed) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * kPi);
    std::uniform_real_distribution<float> radiusDist(0.0f, 0.1f);
    std::uniform_real_distribution<float> heightDist(0.2f, 0.36f);
    std::uniform_real_distribution<float> widthDist(0.05f, 0.08f);
    std::uniform_real_distribution<float> leanDist(-0.09f, 0.09f);
    std::uniform_real_distribution<float> shadeDist(0.7f, 1.3f);
    constexpr int kBladeCount = 6;
    for (int i = 0; i < kBladeCount; ++i) {
        float placementAngle = angleDist(rng);
        float placementRadius = radiusDist(rng);
        glm::vec2 base(std::cos(placementAngle) * placementRadius,
                       std::sin(placementAngle) * placementRadius);
        glm::vec2 lean(leanDist(rng), leanDist(rng));
        glm::vec3 bladeColor = glm::clamp(color * shadeDist(rng), 0.0f, 1.0f);
        appendGrassBlade(vertices, indices, base, heightDist(rng), widthDist(rng), lean,
                        angleDist(rng), bladeColor);
    }
    return Mesh(ctx, commands, vertices, indices);
}

Mesh::Geometry Mesh::treeBarkGeometry(glm::vec3 tint,
                    const TreeGenerator::Tree& tree, int lod) {
    lod = std::clamp(lod, 0, 2);
    float voxelSize = lod == 0 ? .04f : (lod == 1 ? .065f : .1f);
    VoxelSet barkVoxels;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    for (const auto& branch : tree.branches) {
        // Petioles sit inside the leaf groups; their sub-pixel cylinders add
        // substantial upload/draw work in full crowns without useful detail.
        if (branch.baseRadius < .004f || (lod > 0 && branch.baseRadius < .006f)) continue;
        if (branch.baseRadius >= .045f) {
            voxelizeCapsule(barkVoxels, branch.base, branch.tip, branch.baseRadius,
                             branch.tipRadius, voxelSize, branch.base * 17.0f);
        } else {
            // Thin twigs stay connected even when narrower than a voxel.
            appendOrientedFrustum(vertices, indices, branch.base, branch.tip - branch.base,
                                   glm::length(branch.tip - branch.base), branch.baseRadius,
                                   branch.tipRadius, tint, lod == 0 ? 5 : 3, 2.2f);
        }
    }
    VoxelSurface::appendMesh(vertices, indices, barkVoxels, voxelSize, tint, 2.2f);
    return {std::move(vertices), std::move(indices)};
}

Mesh::Geometry Mesh::treeLeafGeometry(glm::vec3 tint,
                      const TreeGenerator::Tree& tree, int lod) {
    lod = std::clamp(lod, 0, 3);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    if (tree.species == TreeGenerator::Species::Oak) {
        for (const auto& leaf : tree.sprays) appendOakLeaf(vertices, indices, leaf, tint, lod);
    } else if (lod == 3) {
        for (const auto& spray : tree.sprays)
            appendLeafSprayProxy(vertices, indices, spray, tint);
    } else {
        VoxelSet cells;
        float voxelSize = lod == 0 ? kNearLeafVoxelSize : (lod == 1 ? .05f : .075f);
        for (const auto& spray : tree.sprays) voxelizeLeafSpray(cells, spray, voxelSize);
        VoxelSurface::appendMesh(vertices, indices, cells, voxelSize, tint, 1.4f);
    }
    return {std::move(vertices), std::move(indices)};
}

Mesh::FoliageGeometry Mesh::treeFoliageGeometry(glm::vec3 tint, const TreeGenerator::Tree& tree) {
    std::map<uint32_t, TreeGenerator::Tree> boughs;
    for (const auto& spray : tree.sprays) {
        auto& bough = boughs[spray.group];
        bough.species = tree.species;
        bough.sprays.push_back(spray);
    }
    FoliageGeometry result;
    for (const auto& [id, bough] : boughs) {
        FoliageGroup group;
        group.seed = id;
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        for (const auto& spray : bough.sprays) {
            // Include the coarse-grid thickness and oak's three laminae.
            glm::vec3 r = spray.radii;
            r.z = std::max(r.z, .075f * .75f);
            glm::vec3 extent = (glm::abs(spray.axes[0])*r.x + glm::abs(spray.axes[1])*r.y
                              + glm::abs(spray.axes[2])*r.z) * 1.1f + glm::vec3(.075f);
            lo = glm::min(lo, spray.center-extent);
            hi = glm::max(hi, spray.center+extent);
        }
        group.center = (lo+hi)*.5f;
        group.radius = glm::length(hi-lo)*.5f;
        group.cullRadius = group.radius;
        for (int lod=0;lod<3;++lod) {
            auto mesh = treeLeafGeometry(tint,bough,lod);
            auto& range=group.levels[lod];
            range.firstIndex=static_cast<uint32_t>(result.mesh.indices.size());
            range.indexCount=static_cast<uint32_t>(mesh.indices.size());
            uint32_t base=static_cast<uint32_t>(result.mesh.vertices.size());
            for (uint32_t index:mesh.indices) result.mesh.indices.push_back(base+index);
            result.mesh.vertices.insert(result.mesh.vertices.end(),mesh.vertices.begin(),mesh.vertices.end());
        }
        group.shadowMedium = group.levels[1];
        group.shadowFar = group.levels[2];
        for (int lod=1; lod<3; ++lod) {
            auto mesh = treeDistantLeafGeometry(tint,bough,lod == 1 ? FoliageLod::kMiddleVoxelSize : FoliageLod::kFarVoxelSize);
            auto& range = group.levels[lod];
            range.firstIndex = static_cast<uint32_t>(result.mesh.indices.size());
            range.indexCount = static_cast<uint32_t>(mesh.indices.size());
            uint32_t base = static_cast<uint32_t>(result.mesh.vertices.size());
            for (uint32_t index : mesh.indices) result.mesh.indices.push_back(base+index);
            for (const auto& vertex : mesh.vertices)
                group.cullRadius = std::max(group.cullRadius,glm::length(vertex.position-group.center));
            result.mesh.vertices.insert(result.mesh.vertices.end(),mesh.vertices.begin(),mesh.vertices.end());
        }
        result.groups.push_back(group);
    }
    return result;
}

Mesh::Geometry Mesh::treeDistantBarkGeometry(glm::vec3 tint, const TreeGenerator::Tree& tree) {
    Geometry result;
    for (const auto& branch : tree.branches) {
        // Keep trunk and principal boughs; sub-pixel twigs account for most
        // of the old far bark's geometry and are hidden inside the crown.
        if (branch.baseRadius < .045f) continue;
        glm::vec3 direction = branch.tip-branch.base;
        float length = glm::length(direction);
        if (length < 1e-5f) continue;
        appendOrientedFrustum(result.vertices,result.indices,branch.base,direction,length,
            branch.baseRadius,branch.tipRadius,tint,4,2.2f);
    }
    return result;
}

Mesh::Geometry Mesh::treeDistantLeafGeometry(glm::vec3 tint, const TreeGenerator::Tree& tree,
                                              float voxelSize) {
    VoxelSet cells;
    for (const auto& spray : tree.sprays) {
        voxelizeLeafSpray(cells,spray,voxelSize);
        // Fine sprays can fall between sample centers on this grid. Retain
        // their occupied cell, merging neighbors without dropping a bough.
        cells.insert(glm::ivec3(glm::floor(spray.center/voxelSize)));
    }
    Geometry result;
    VoxelSurface::appendMesh(result.vertices,result.indices,cells,voxelSize,tint,1.4f);
    return result;
}

void Mesh::bindAndDrawIndirect(VkCommandBuffer cmd, VkBuffer commands, VkDeviceSize offset,
                               uint32_t count) const {
    VkBuffer buffers[]={vertexBuffer_.handle()};
    VkDeviceSize offsets[]={0};
    vkCmdBindVertexBuffers(cmd,0,1,buffers,offsets);
    vkCmdBindIndexBuffer(cmd,indexBuffer_.handle(),0,VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(cmd,commands,offset,count,sizeof(VkDrawIndexedIndirectCommand));
    ++DrawStatistics::calls;
}
void Mesh::bindAndDrawRange(VkCommandBuffer cmd, const VkDrawIndexedIndirectCommand& draw) const {
    VkBuffer buffers[]={vertexBuffer_.handle()};
    VkDeviceSize offsets[]={0};
    vkCmdBindVertexBuffers(cmd,0,1,buffers,offsets);
    vkCmdBindIndexBuffer(cmd,indexBuffer_.handle(),0,VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd,draw.indexCount,draw.instanceCount,draw.firstIndex,draw.vertexOffset,draw.firstInstance);
    ++DrawStatistics::calls;
}
