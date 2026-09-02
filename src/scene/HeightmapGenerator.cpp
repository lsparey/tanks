#include "HeightmapGenerator.h"

#include <algorithm>
#include <cmath>
#include <random>

#include <glm/glm.hpp>

namespace {

// Same hash/smoothNoise/fbm shape used by the texture generators (see
// GrassTextureGenerator.cpp) -- not tileable/periodic here since the
// heightmap is a single finite grid, not a repeating texture, so there's no
// wrapLattice step.
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

float fbm(float x, float y, int octaves) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float total = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * smoothNoise2D(x * frequency, y * frequency);
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / total;  // [0, 1]
}

// One step of the traced river path -- grid indices (so the carve pass can
// stamp a local window around it cheaply) plus the *base* height at that
// point (before any carving), which is what drives both the "only below the
// halfway mark" gate and the "deeper where the ground is already lower"
// scaling in carveRiver.
struct RiverPathPoint {
    int i, j;
    float baseHeight;
};

// Steepest-descent walk from (startI, startJ): at each step, move to
// whichever of the 8 neighbors has the lowest height, stopping the moment
// there is no lower neighbor (a local minimum) or the map edge is reached.
// Height strictly decreases every single step by construction, so the
// result is a path that only ever proceeds downward from its starting
// position -- never uphill, never level -- and always terminates (no
// cycles are possible when every step strictly decreases a bounded value).
std::vector<RiverPathPoint> traceRiverPath(const std::vector<float>& baseHeights, int resolution,
                                            int startI, int startJ) {
    std::vector<RiverPathPoint> path;
    int ci = startI, cj = startJ;
    // Bounded by construction (see above), but cap anyway as a defensive
    // backstop against surprises rather than trusting that reasoning alone.
    int maxSteps = resolution * 4;
    for (int step = 0; step < maxSteps; ++step) {
        float curHeight = baseHeights[static_cast<size_t>(cj) * resolution + ci];
        path.push_back({ci, cj, curHeight});

        int bestI = ci, bestJ = cj;
        float bestHeight = curHeight;
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                if (di == 0 && dj == 0) continue;
                int ni = ci + di, nj = cj + dj;
                if (ni < 0 || ni >= resolution || nj < 0 || nj >= resolution) continue;
                float nh = baseHeights[static_cast<size_t>(nj) * resolution + ni];
                if (nh < bestHeight) {
                    bestHeight = nh;
                    bestI = ni;
                    bestJ = nj;
                }
            }
        }
        if (bestI == ci && bestJ == cj) break;  // local minimum -- nowhere lower to go
        ci = bestI;
        cj = bestJ;
    }
    return path;
}

// Carves the traced path into `heights` in place. Gated to strictly below
// riverMidHeight (the halfway point between the base terrain's own lowest
// and highest point) -- a path point at or above it carves nothing at all,
// which is what keeps the river confined to low ground instead of cutting
// across the plateau/hilltops it happened to start near. Below the
// midpoint, depth scales with how far below it the point's own base height
// already is (0 right at the midpoint, deepest at the terrain's actual
// lowest point) -- shallower on higher ground, deeper in low ground, per
// the same reasoning.
void carveRiver(std::vector<float>& heights, int resolution, float worldSize,
                 const std::vector<RiverPathPoint>& path, float riverMidHeight, float baseMinHeight,
                 float maxCarveDepth, float channelWidth, float spawnClearRadius) {
    float cellSize = worldSize / static_cast<float>(resolution - 1);
    int radiusCells = static_cast<int>(std::ceil(channelWidth / cellSize)) + 1;
    float heightSpan = std::max(riverMidHeight - baseMinHeight, 0.001f);

    std::vector<float> carve(heights.size(), 0.0f);
    for (const RiverPathPoint& p : path) {
        if (p.baseHeight >= riverMidHeight) continue;  // upper half of the terrain -- no river here
        float depthT = (riverMidHeight - p.baseHeight) / heightSpan;
        float carveDepthHere = glm::clamp(depthT, 0.0f, 1.0f) * maxCarveDepth;

        // Same spawn-clearing idea HeightmapGenerator's plateau/river used
        // before -- fades the carve to 0 within ~12 units of the world
        // origin so the tank's spawn point is never sitting in a trench,
        // regardless of where the traced path happens to run.
        float wx = (static_cast<float>(p.i) / (resolution - 1) - 0.5f) * worldSize;
        float wz = (static_cast<float>(p.j) / (resolution - 1) - 0.5f) * worldSize;
        float distFromSpawn = std::sqrt(wx * wx + wz * wz);
        carveDepthHere *= glm::clamp((distFromSpawn - spawnClearRadius) / 10.0f, 0.0f, 1.0f);
        if (carveDepthHere <= 0.0f) continue;

        for (int dj = -radiusCells; dj <= radiusCells; ++dj) {
            int nj = p.j + dj;
            if (nj < 0 || nj >= resolution) continue;
            for (int di = -radiusCells; di <= radiusCells; ++di) {
                int ni = p.i + di;
                if (ni < 0 || ni >= resolution) continue;
                float worldDist = std::sqrt(static_cast<float>(di * di + dj * dj)) * cellSize;
                float falloff = 1.0f - glm::smoothstep(0.0f, channelWidth, worldDist);
                if (falloff <= 0.0f) continue;
                size_t idx = static_cast<size_t>(nj) * resolution + ni;
                // Max rather than sum -- consecutive path points are only
                // about one cell apart, so their stamps overlap heavily;
                // summing would double/triple-carve the same ground instead
                // of tracing a single channel of consistent depth.
                carve[idx] = std::max(carve[idx], carveDepthHere * falloff);
            }
        }
    }

    for (size_t idx = 0; idx < heights.size(); ++idx) heights[idx] -= carve[idx];
}

}  // namespace

HeightmapGenerator::Heightmap HeightmapGenerator::generateHills(int resolution, float worldSize,
                                                                 float amplitude, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> freqDist(0.04f, 0.18f);
    std::uniform_real_distribution<float> phaseDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> offsetDist(0.0f, 1000.0f);

    // Base gentle rolling hills -- same two-sine shape as the original
    // generator. This alone still covers most of the map (and, with the
    // spawn-clearing below, all of the area right around the tank's spawn
    // at the world origin), with the plateau/river layered on top farther
    // out.
    float freqX1 = freqDist(rng), freqZ1 = freqDist(rng);
    float phaseX1 = phaseDist(rng), phaseZ1 = phaseDist(rng);
    float freqX2 = freqDist(rng), freqZ2 = freqDist(rng);
    float phaseX2 = phaseDist(rng), phaseZ2 = phaseDist(rng);

    // Plateau field: a broad, low-frequency noise used as a mask -- above
    // plateauMaskThreshold it ramps a raised region up to a near-flat top
    // (compressed rather than hard-clamped, so the top still has a little
    // texture instead of reading as a perfectly flat table).
    float plateauFreq = 0.02f;
    float plateauOffsetX = offsetDist(rng);
    float plateauOffsetY = offsetDist(rng);

    Heightmap map;
    map.resolution = resolution;
    map.worldSize = worldSize;
    map.heights.resize(static_cast<size_t>(resolution) * resolution);

    // Pass 1: base terrain (rolling hills + plateau), no river yet -- the
    // river trace below needs a complete height field to walk downhill
    // across, and "below the halfway point" needs the base terrain's own
    // actual min/max, so this has to be a separate pass rather than folded
    // into the same per-cell loop the original single-pass generator used.
    for (int j = 0; j < resolution; ++j) {
        for (int i = 0; i < resolution; ++i) {
            float x = (static_cast<float>(i) / (resolution - 1) - 0.5f) * worldSize;
            float z = (static_cast<float>(j) / (resolution - 1) - 0.5f) * worldSize;

            float baseHills = amplitude * 0.6f * std::sin(x * freqX1 + phaseX1) *
                                   std::cos(z * freqZ1 + phaseZ1) +
                               amplitude * 0.4f * std::sin(x * freqX2 + phaseX2) *
                                   std::sin(z * freqZ2 + phaseZ2);

            // Ramps 0 near the tank's spawn (world origin) up to 1 by
            // ~22 units out, so the plateau never appears right at spawn
            // (a cliff edge there would look broken and could strand the
            // tank) -- everywhere else gets the full effect.
            float distFromSpawn = std::sqrt(x * x + z * z);
            float featureStrength = glm::clamp((distFromSpawn - 12.0f) / 10.0f, 0.0f, 1.0f);

            float plateauNoise =
                fbm(x * plateauFreq + plateauOffsetX, z * plateauFreq + plateauOffsetY, 3);
            float plateauMaskThreshold = 0.58f;
            float plateauMask =
                glm::smoothstep(plateauMaskThreshold, plateauMaskThreshold + 0.32f, plateauNoise);
            // Raw (uncompressed) height the plateau would reach at full
            // mask strength; compressed above flatCap so the interior
            // reads as a near-flat tabletop instead of just another hill.
            float plateauRaw = amplitude * 1.1f;
            float flatCap = amplitude * 0.95f;
            float plateauHeight =
                plateauRaw > flatCap ? flatCap + (plateauRaw - flatCap) * 0.1f : plateauRaw;
            float plateau = plateauMask * plateauHeight * featureStrength;

            map.heights[static_cast<size_t>(j) * resolution + i] = baseHills + plateau;
        }
    }

    // Pass 2: trace a river as an actual downhill path across the base
    // terrain above (steepest descent -- see traceRiverPath), rather than a
    // per-pixel noise field with no notion of "uphill"/"downhill" at all.
    // Start from whichever of a handful of random candidates sits highest,
    // for a longer/more interesting descent than a single random guess
    // would usually give.
    float baseMin = map.heights[0], baseMax = map.heights[0];
    for (float h : map.heights) {
        baseMin = std::min(baseMin, h);
        baseMax = std::max(baseMax, h);
    }
    float riverMidHeight = (baseMin + baseMax) * 0.5f;

    std::uniform_int_distribution<int> gridDist(0, resolution - 1);
    int startI = 0, startJ = 0;
    float startHeight = -1e9f;
    constexpr int kStartCandidates = 10;
    for (int c = 0; c < kStartCandidates; ++c) {
        int ci = gridDist(rng), cj = gridDist(rng);
        float h = map.heights[static_cast<size_t>(cj) * resolution + ci];
        if (h > startHeight) {
            startHeight = h;
            startI = ci;
            startJ = cj;
        }
    }

    std::vector<RiverPathPoint> riverPath = traceRiverPath(map.heights, resolution, startI, startJ);
    carveRiver(map.heights, resolution, worldSize, riverPath, riverMidHeight, baseMin,
               /*maxCarveDepth=*/amplitude * 0.9f, /*channelWidth=*/3.0f, /*spawnClearRadius=*/12.0f);

    return map;
}
