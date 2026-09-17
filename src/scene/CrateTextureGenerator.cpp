#include "CrateTextureGenerator.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/glm.hpp>

namespace {

// Non-tileable value noise -- fine here since this texture is mapped
// exactly once per cube face (UV spans [0,1] with no repeat), unlike the
// other generators.
float hash(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

float smoothNoise(float x, float y) {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    float tx = x - static_cast<float>(x0);
    float ty = y - static_cast<float>(y0);
    float sx = tx * tx * (3.0f - 2.0f * tx);
    float sy = ty * ty * (3.0f - 2.0f * ty);
    float n00 = hash(x0, y0);
    float n10 = hash(x0 + 1, y0);
    float n01 = hash(x0, y0 + 1);
    float n11 = hash(x0 + 1, y0 + 1);
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
        sum += amplitude * smoothNoise(x * frequency, y * frequency);
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / total;
}

}  // namespace

std::vector<uint8_t> CrateTextureGenerator::generate(uint32_t size) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    const glm::vec3 lightWood(0.66f, 0.50f, 0.30f);
    const glm::vec3 darkWood(0.38f, 0.26f, 0.145f);
    const glm::vec3 seamColor(0.14f, 0.09f, 0.045f);
    const glm::vec3 nailColor(0.28f, 0.27f, 0.26f);

    // Planks are stacked vertically (along v); grain runs horizontally
    // (along u) within each plank. Each plank carries its own board tone and
    // grain phase (real crates are nailed together from whatever boards were
    // on hand, and identical planks were the old texture's biggest tell),
    // plus a chance of a knot. A darker perimeter frame with corner nails
    // reads as the crate's batten framing.
    constexpr int kPlankCount = 4;
    constexpr float kSeamHalfWidth = 0.03f;
    constexpr float kBorderWidth = 0.055f;

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);

            int plank = std::min(static_cast<int>(v * kPlankCount), kPlankCount - 1);
            float vv = v * kPlankCount - static_cast<float>(plank);  // 0..1 within this plank

            // Board-to-board variation: brightness and a slight warm/grey
            // shift per plank.
            float plankTone = 0.84f + hash(plank * 17 + 3, 41) * 0.32f;
            float plankHue = hash(plank * 29 + 7, 91);

            // Grain: long streaks along u, wobbled by a low-frequency warp so
            // the streak lines waver like real figure instead of running
            // ruler-straight, plus a finer streak layer for close-up detail.
            float wobble = fbm(u * 2.0f + static_cast<float>(plank) * 13.7f, vv * 3.0f, 2);
            float grain = fbm(u * 3.5f + static_cast<float>(plank) * 31.9f,
                              (vv + wobble * 0.4f) * 9.0f, 3);
            float streak = fbm(u * 16.0f + static_cast<float>(plank) * 77.1f, vv * 34.0f, 2);
            float g = glm::clamp(grain * 0.68f + streak * 0.32f, 0.0f, 1.0f);
            // Sharpen: pushes the blended noise toward distinct light/dark
            // grain lines rather than a soft brown blur.
            g = glm::smoothstep(0.24f, 0.78f, g);
            glm::vec3 color = glm::mix(darkWood, lightWood, g) * plankTone;
            color = glm::mix(color, color * glm::vec3(1.06f, 0.96f, 0.86f), plankHue * 0.6f);

            // Knots: up to two candidate spots per plank, each present about
            // half the time -- a dark core with faint growth rings, and the
            // surrounding grain darkened slightly like wood around a knot.
            for (int k = 0; k < 2; ++k) {
                if (hash(plank * 67 + k * 131, 9) > 0.5f) continue;
                float ku = 0.14f + hash(plank * 53 + k * 101, 5) * 0.72f;
                float kv = 0.28f + hash(plank * 91 + k * 163, 3) * 0.44f;
                // u compressed so the knot stays roundish in texel space
                // despite the plank being 4x wider than tall.
                float dist =
                    glm::length(glm::vec2((u - ku) * static_cast<float>(kPlankCount), vv - kv)) /
                    0.17f;
                if (dist >= 1.8f) continue;
                float rings = 0.5f + 0.5f * std::sin(dist * 8.0f + ku * 40.0f);
                float knotMask = 1.0f - glm::smoothstep(0.55f, 1.15f, dist);
                glm::vec3 knotColor = glm::mix(darkWood * 0.5f, darkWood * 0.95f, rings);
                color = glm::mix(color, knotColor, knotMask);
                color = glm::mix(color, seamColor, 1.0f - glm::smoothstep(0.0f, 0.3f, dist));
                // Faint halo of compressed grain just outside the knot.
                float halo = glm::smoothstep(1.0f, 1.25f, dist) *
                             (1.0f - glm::smoothstep(1.25f, 1.8f, dist));
                color *= 1.0f - halo * 0.12f;
            }

            // Darken toward each plank boundary, with a thin catch-light just
            // above the seam so the boards read as separate slats with real
            // thickness instead of painted-on lines.
            float plankV = v * static_cast<float>(kPlankCount);
            float distToSeam = std::abs(plankV - std::round(plankV));
            float seamDarken = 1.0f - glm::smoothstep(0.0f, kSeamHalfWidth, distToSeam);
            float bevel = glm::smoothstep(kSeamHalfWidth, kSeamHalfWidth * 2.4f, distToSeam) *
                          (1.0f - glm::smoothstep(kSeamHalfWidth * 2.4f, kSeamHalfWidth * 5.0f,
                                                  distToSeam));
            color = glm::mix(color, seamColor, seamDarken);
            color *= 1.0f + bevel * 0.14f;

            // Perimeter frame: its own slightly darker boards with grain
            // running around the face (v-major), separated from the field
            // planks by a shadow line at the inner frame edge.
            float edgeDist = std::min({u, 1.0f - u, v, 1.0f - v});
            float frameMask = 1.0f - glm::smoothstep(kBorderWidth * 0.9f, kBorderWidth, edgeDist);
            float frameGrain = glm::smoothstep(
                0.2f, 0.8f, fbm(v * 3.2f + 51.7f, u * 24.0f, 3));
            glm::vec3 frameColor = glm::mix(darkWood, lightWood, frameGrain) * 0.72f;
            color = glm::mix(color, frameColor, frameMask * 0.9f);
            float frameLine =
                1.0f - glm::smoothstep(0.004f, 0.014f, std::abs(edgeDist - kBorderWidth));
            color = glm::mix(color, seamColor, frameLine * 0.65f);

            // Nail heads pinning the frame: one near each corner and one at
            // each edge midpoint, a small dark disc with an off-centre
            // highlight so it reads as a domed head catching the light.
            const glm::vec2 nailPositions[8] = {
                {0.028f, 0.028f}, {0.5f, 0.028f}, {0.972f, 0.028f}, {0.028f, 0.5f},
                {0.972f, 0.5f},   {0.028f, 0.972f}, {0.5f, 0.972f}, {0.972f, 0.972f},
            };
            for (const glm::vec2& nail : nailPositions) {
                glm::vec2 d(u - nail.x, v - nail.y);
                float dist = glm::length(d);
                float mask = 1.0f - glm::smoothstep(0.008f, 0.014f, dist);
                if (mask <= 0.0f) continue;
                color = glm::mix(color, nailColor, mask);
                float highlight =
                    1.0f - glm::smoothstep(0.0f, 0.006f, glm::length(d + glm::vec2(0.004f)));
                color += glm::vec3(0.22f) * highlight;
            }

            size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}

namespace {

// Signed-distance primitives in icon space: the face's centre is the origin
// and a unit distance is kIconRadius of the face, so every icon is designed
// inside roughly the unit disc and sits clear of the frame/nails.
constexpr float kIconRadius = 0.30f;

float sdCircle(glm::vec2 p, glm::vec2 c, float r) { return glm::length(p - c) - r; }
float sdRoundedBox(glm::vec2 p, glm::vec2 c, glm::vec2 half, float r) {
    glm::vec2 q = glm::abs(p - c) - half + glm::vec2(r);
    return glm::length(glm::max(q, glm::vec2(0.0f))) + std::min(std::max(q.x, q.y), 0.0f) - r;
}
float sdSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b, float thickness) {
    glm::vec2 pa = p - a, ba = b - a;
    float h = glm::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
    return glm::length(pa - ba * h) - thickness;
}
float sdRing(glm::vec2 p, glm::vec2 c, float r, float thickness) {
    return std::abs(glm::length(p - c) - r) - thickness;
}
// Filled triangle: for a convex shape the distance is the largest of the
// signed distances to its edge lines (exact inside, an upper bound outside
// near the corners -- adequate at the anti-aliasing widths used here).
float sdTriangle(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
    // Orient CCW so every inward-facing edge normal points the same way.
    if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) < 0.0f) std::swap(b, c);
    float d = -1e9f;
    for (auto [e0, e1] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, a}}) {
        glm::vec2 edge = e1 - e0;
        glm::vec2 outward = glm::normalize(glm::vec2(edge.y, -edge.x));
        d = std::max(d, glm::dot(p - e0, outward));
    }
    return d;
}

}  // namespace

// Distance (negative inside) to the power-up's glyph. Each is a small
// composition of the primitives above; shapes were chosen to read at a
// glance from tank distance and to differ in silhouette, not just colour.
float CrateTextureGenerator::iconDistance(PowerUpType type, glm::vec2 p) {
    switch (type) {
        case PowerUpType::ExtraShell: {
            // Bullet-shaped shell (rounded nose, squared base with a driving
            // band) with a "+" beside it.
            glm::vec2 s(-0.28f, 0.05f);
            float body = sdRoundedBox(p, s + glm::vec2(0.0f, 0.15f), glm::vec2(0.20f, 0.42f), 0.05f);
            float nose = sdCircle(p, s + glm::vec2(0.0f, -0.30f), 0.20f);
            float band = sdRoundedBox(p, s + glm::vec2(0.0f, 0.36f), glm::vec2(0.26f, 0.06f), 0.02f);
            float shell = std::min({body, nose, band});
            glm::vec2 c(0.48f, -0.30f);
            float plus = std::min(sdRoundedBox(p, c, glm::vec2(0.27f, 0.07f), 0.02f),
                                  sdRoundedBox(p, c, glm::vec2(0.07f, 0.27f), 0.02f));
            return std::min(shell, plus);
        }
        case PowerUpType::IncreasedDamage: {
            // Eight-point starburst: a blast.
            float r = glm::length(p);
            float a = std::atan2(p.y, p.x);
            float spikes = 0.45f + 0.45f * (0.5f + 0.5f * std::cos(8.0f * a));
            return r - spikes;
        }
        case PowerUpType::LargerSplash: {
            // Expanding shockwave: a core plus two rings, the outer one heavier.
            return std::min({sdCircle(p, glm::vec2(0.0f), 0.16f), sdRing(p, glm::vec2(0.0f), 0.50f, 0.06f),
                             sdRing(p, glm::vec2(0.0f), 0.86f, 0.09f)});
        }
        case PowerUpType::AimAssist: {
            // Ballistic arc ending in an arrowhead -- literally what the
            // power-up draws on the HUD. A solid polyline rather than dots:
            // at crate-on-the-horizon sizes a line leads the eye into the
            // arrowhead, where detached dots read as a separate mark.
            auto arc = [](float t) {
                float x = -0.88f + 1.76f * t;
                float k = 2.0f * t - 1.0f;
                return glm::vec2(x, 0.42f - 0.85f * (1.0f - k * k));
            };
            constexpr int kSegments = 12;
            constexpr float kLineEnd = 0.84f;  // the arrowhead covers the rest
            float d = 1e9f;
            for (int i = 0; i < kSegments; ++i) {
                float t0 = kLineEnd * static_cast<float>(i) / kSegments;
                float t1 = kLineEnd * static_cast<float>(i + 1) / kSegments;
                d = std::min(d, sdSegment(p, arc(t0), arc(t1), 0.07f));
            }
            glm::vec2 tip = arc(1.0f);
            glm::vec2 dir = glm::normalize(tip - arc(0.80f));
            glm::vec2 perp(-dir.y, dir.x);
            glm::vec2 back = tip - dir * 0.40f;
            return std::min(d, sdTriangle(p, tip, back + perp * 0.21f, back - perp * 0.21f));
        }
        case PowerUpType::TighterAccuracy: {
            // Crosshair: ring, centre dot and four ticks crossing the ring.
            float d = std::min(sdRing(p, glm::vec2(0.0f), 0.60f, 0.08f), sdCircle(p, glm::vec2(0.0f), 0.14f));
            for (glm::vec2 axis : {glm::vec2(1, 0), glm::vec2(0, 1)})
                d = std::min({d, sdSegment(p, axis * 0.42f, axis * 0.92f, 0.08f),
                              sdSegment(p, -axis * 0.42f, -axis * 0.92f, 0.08f)});
            return d;
        }
        case PowerUpType::MoreFuel: {
            // Jerry can: body with the pressed "X" ribs, top handle and spout.
            glm::vec2 c(0.0f, 0.14f);
            float body = sdRoundedBox(p, c, glm::vec2(0.52f, 0.52f), 0.09f);
            float handle = sdRoundedBox(p, glm::vec2(-0.12f, -0.50f), glm::vec2(0.34f, 0.10f), 0.05f);
            float spout = sdRoundedBox(p, glm::vec2(0.40f, -0.52f), glm::vec2(0.10f, 0.14f), 0.03f);
            float d = std::min({body, handle, spout});
            // Ribs are cut out of the body (wood shows through) rather than
            // painted, so the can keeps a solid outline.
            float rib = std::min(sdSegment(p, c + glm::vec2(-0.30f, -0.30f), c + glm::vec2(0.30f, 0.30f), 0.06f),
                                 sdSegment(p, c + glm::vec2(-0.30f, 0.30f), c + glm::vec2(0.30f, -0.30f), 0.06f));
            return std::max(d, -rib);
        }
    }
    return 1e9f;
}

glm::vec3 CrateTextureGenerator::iconPaint(PowerUpType type) {
    switch (type) {
        case PowerUpType::ExtraShell: return {0.92f, 0.92f, 0.88f};      // white
        case PowerUpType::IncreasedDamage: return {0.82f, 0.16f, 0.12f}; // red
        case PowerUpType::LargerSplash: return {0.93f, 0.52f, 0.12f};    // orange
        case PowerUpType::AimAssist: return {0.30f, 0.78f, 0.72f};       // teal (HUD accent)
        case PowerUpType::TighterAccuracy: return {0.35f, 0.72f, 0.28f}; // green
        case PowerUpType::MoreFuel: return {0.30f, 0.55f, 0.92f};        // blue (HUD fuel gauge)
    }
    return glm::vec3(1.0f);
}

std::vector<uint8_t> CrateTextureGenerator::stampIcon(const std::vector<uint8_t>& base, uint32_t size,
                                                      PowerUpType type) {
    std::vector<uint8_t> pixels = base;
    const glm::vec3 paint = iconPaint(type);
    // Anti-aliasing width of ~1.2 texels, expressed in icon units.
    const float aa = 1.2f / (static_cast<float>(size) * kIconRadius);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            glm::vec2 p = (glm::vec2(u, v) - glm::vec2(0.5f)) / kIconRadius;
            float d = iconDistance(type, p);
            if (d >= aa) continue;
            float coverage = 1.0f - glm::smoothstep(-aa, aa, d);
            // Spray-stencil wear: thin the paint over the grain's ridges so it
            // sits on the wood rather than floating over it.
            float wear = 0.78f + 0.22f * fbm(u * 38.0f + 5.3f, v * 38.0f + 2.1f, 2);
            size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            glm::vec3 wood(pixels[idx] / 255.0f, pixels[idx + 1] / 255.0f, pixels[idx + 2] / 255.0f);
            float lum = glm::dot(wood, glm::vec3(0.3f, 0.59f, 0.11f));
            glm::vec3 color = glm::mix(wood, paint * (0.80f + 0.45f * lum), coverage * wear);
            pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
        }
    }
    return pixels;
}
