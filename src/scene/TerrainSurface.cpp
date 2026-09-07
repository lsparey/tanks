#include "TerrainSurface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

TerrainSurface::TerrainSurface(HeightmapGenerator::Heightmap heightmap)
    : heightmap_(std::move(heightmap)) {
    const auto& hm = heightmap_;
    if (hm.resolution < 2 || !std::isfinite(hm.worldSize) || hm.worldSize <= 0 ||
        static_cast<uint64_t>(hm.resolution) * hm.resolution >
            std::numeric_limits<uint32_t>::max() ||
        hm.heights.size() != static_cast<size_t>(hm.resolution) * hm.resolution)
        throw std::invalid_argument("invalid terrain grid dimensions or height count");
    for (float height : hm.heights) {
        if (!std::isfinite(height)) throw std::invalid_argument("non-finite terrain height");
    }
    float spacing = hm.worldSize / (hm.resolution - 1);
    if (!std::isfinite(1.0f / spacing))
        throw std::invalid_argument("terrain spacing is too small");
    shadingNormals_.resize(hm.heights.size());
    for (int z = 0; z < hm.resolution; ++z) {
        for (int x = 0; x < hm.resolution; ++x) {
            int xp = std::max(x - 1, 0), xn = std::min(x + 1, hm.resolution - 1);
            int zp = std::max(z - 1, 0), zn = std::min(z + 1, hm.resolution - 1);
            // Double intermediates also keep finite, extreme input heights
            // from overflowing the normal calculation.
            double dx = (double(hm.at(xn, z)) - hm.at(xp, z)) / ((xn - xp) * double(spacing));
            double dz = (double(hm.at(x, zn)) - hm.at(x, zp)) / ((zn - zp) * double(spacing));
            shadingNormals_[static_cast<size_t>(z) * hm.resolution + x] =
                glm::vec3(glm::normalize(glm::dvec3(-dx, 1.0, -dz)));
        }
    }
}

glm::vec3 TerrainSurface::position(int x, int z) const {
    const auto& hm = heightmap_;
    return {(float(x) / (hm.resolution - 1) - 0.5f) * hm.worldSize,
            hm.at(x, z), (float(z) / (hm.resolution - 1) - 0.5f) * hm.worldSize};
}

std::array<uint32_t, 6> TerrainSurface::quadIndices(int resolution, int x, int z) {
    uint32_t v00 = static_cast<uint32_t>(z) * resolution + x;
    uint32_t v01 = v00 + resolution;
    return {v00, v01, v01 + 1, v00, v01 + 1, v00 + 1};
}

TerrainSurface::Sample TerrainSurface::sampleAt(float worldX, float worldZ) const {
    if (!std::isfinite(worldX) || !std::isfinite(worldZ))
        throw std::invalid_argument("non-finite terrain query");
    const auto& hm = heightmap_;
    // Clamp before scaling so finite, far-outside queries cannot overflow.
    float gx = (std::clamp(worldX / hm.worldSize, -0.5f, 0.5f) + 0.5f) * (hm.resolution - 1);
    float gz = (std::clamp(worldZ / hm.worldSize, -0.5f, 0.5f) + 0.5f) * (hm.resolution - 1);
    int x = std::min(static_cast<int>(gx), hm.resolution - 2);
    int z = std::min(static_cast<int>(gz), hm.resolution - 2);
    float tx = gx - x, tz = gz - z;
    auto indices = quadIndices(hm.resolution, x, z);
    if (tz >= tx)
        return {{indices[0], indices[1], indices[2]}, {1.0f - tz, tz - tx, tx}};
    return {{indices[3], indices[4], indices[5]}, {1.0f - tx, tz, tx - tz}};
}

float TerrainSurface::heightAt(float worldX, float worldZ) const {
    auto sample = sampleAt(worldX, worldZ);
    return static_cast<float>(double(heightmap_.heights[sample.indices[0]]) * sample.weights.x +
                             double(heightmap_.heights[sample.indices[1]]) * sample.weights.y +
                             double(heightmap_.heights[sample.indices[2]]) * sample.weights.z);
}

glm::vec3 TerrainSurface::contactNormalAt(float worldX, float worldZ) const {
    auto sample = sampleAt(worldX, worldZ);
    auto point = [&](uint32_t index) {
        return glm::dvec3(position(index % heightmap_.resolution, index / heightmap_.resolution));
    };
    glm::dvec3 a = point(sample.indices[0]);
    return glm::vec3(glm::normalize(glm::cross(point(sample.indices[1]) - a,
                                               point(sample.indices[2]) - a)));
}

glm::vec3 TerrainSurface::shadingNormalAt(float worldX, float worldZ) const {
    auto sample = sampleAt(worldX, worldZ);
    return glm::normalize(shadingNormals_[sample.indices[0]] * sample.weights.x +
                          shadingNormals_[sample.indices[1]] * sample.weights.y +
                          shadingNormals_[sample.indices[2]] * sample.weights.z);
}
