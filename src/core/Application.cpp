#include "Application.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

#include "../render/Buffer.h"
#include "../render/ScreenshotWriter.h"
#include "../render/VulkanCheck.h"
#include "../scene/BarkTextureGenerator.h"
#include "../scene/BoundaryGenerator.h"
#include "../scene/BoundaryTextureGenerator.h"
#include "../scene/CamoTextureGenerator.h"
#include "../scene/CloudTextureGenerator.h"
#include "../scene/CollisionSystem.h"
#include "../scene/CrateTextureGenerator.h"
#include "../scene/GrassTextureGenerator.h"
#include "../scene/LeafTextureGenerator.h"
#include "../scene/MetalTextureGenerator.h"
#include "../scene/RockTextureGenerator.h"
#include "../scene/TrackTextureGenerator.h"
#include "../scene/WaterGenerator.h"

namespace {

constexpr uint32_t kWindowWidth = 1280;
constexpr uint32_t kWindowHeight = 720;
constexpr uint32_t kGpuTimestampsPerFrame = 8;

bool sphereIntersectsFrustum(const glm::mat4& viewProjection, glm::vec3 center, float radius) {
    auto row = [&](int r) {
        return glm::vec4(viewProjection[0][r], viewProjection[1][r],
                         viewProjection[2][r], viewProjection[3][r]);
    };
    glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    // GLM is configured for Vulkan's [0,w] clip-depth range. The other four
    // planes retain the conventional [-w,w] X/Y clip bounds.
    const glm::vec4 planes[] = {
        r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2,
    };
    for (const glm::vec4& plane : planes) {
        float normalLength = glm::length(glm::vec3(plane));
        if (glm::dot(glm::vec3(plane), center) + plane.w < -radius * normalLength) return false;
    }
    return true;
}

float projectedRadiusPixels(const glm::mat4& view, const glm::mat4& projection,
                            float viewportHeight, glm::vec3 center, float radius) {
    glm::vec4 viewCenter = view * glm::vec4(center, 1.0f);
    float depth = -viewCenter.z;
    if (depth <= 0.001f) return std::numeric_limits<float>::max();
    return std::abs(projection[1][1]) * viewportHeight * 0.5f * radius / depth;
}

int selectLodWithHysteresis(int currentLod, float projectedRadius, float nearThreshold,
                            float farThreshold) {
    constexpr float kDownshiftScale = 0.88f;
    constexpr float kUpshiftScale = 1.12f;
    currentLod = std::clamp(currentLod, 0, 2);
    if (currentLod == 0) {
        if (projectedRadius < farThreshold * kDownshiftScale) return 2;
        if (projectedRadius < nearThreshold * kDownshiftScale) return 1;
        return 0;
    }
    if (currentLod == 1) {
        if (projectedRadius > nearThreshold * kUpshiftScale) return 0;
        if (projectedRadius < farThreshold * kDownshiftScale) return 2;
        return 1;
    }
    if (projectedRadius > nearThreshold * kUpshiftScale) return 0;
    if (projectedRadius > farThreshold * kUpshiftScale) return 1;
    return 2;
}

// Terrain height below which a basin can start filling with water -- tuned
// against the heightmap's actual range (roughly -2.2..-4 on the low end,
// seed-dependent, now that HeightmapGenerator layers a plateau and a
// steepest-descent-traced river valley on top of the base rolling hills,
// versus the plain +-2.2 of a hills-only field), and against the rock
// texture's own rockyThreshold blend band in basic.frag, so water sits
// within the already-rocky lowest terrain (the river's deepest points)
// rather than on obviously grassy ground.
constexpr float kWaterThreshold = -1.9f;
// How deep any single body of water is allowed to get above its own basin
// floor -- kept shallow per the user's request, and this is also what
// keeps separate basins from all settling at one shared "sea level" (see
// WaterGenerator). Nudged up slightly from the old 0.6 alongside the wider
// height range above.
constexpr float kWaterMaxDepth = 0.9f;

// How far in from the terrain's actual edge the play-area boundary sits,
// as a fraction of the terrain's total width (see BoundaryGenerator).
constexpr float kBoundaryInsetFraction = 0.1f;
// Tall enough to clear the tallest procedural trees (Mesh::treeBark/
// treeLeaves puts those at roughly 2-5 world units depending on their
// random instance scale -- see Application::spawnTrees) with comfortable
// margin, so the wall of light reads as taller than the treeline rather
// than poking out partway through it.
constexpr float kBoundaryWallHeight = 7.0f;

// Digits are drawn as seven-segment glyphs made of HudRenderer quads --
// there's no font/text rendering in the HUD, and a segmented display is the
// simplest thing that composes out of the axis-aligned rectangles it already
// draws (crosshair, box ticks).
constexpr float kDigitSegmentThickness = 0.006f;

const bool kSevenSegmentTable[10][7] = {
    // a(top), b(top-right), c(bottom-right), d(bottom), e(bottom-left), f(top-left), g(middle)
    {true, true, true, true, true, true, false},    // 0
    {false, true, true, false, false, false, false},  // 1
    {true, true, false, true, true, false, true},    // 2
    {true, true, true, true, false, false, true},    // 3
    {false, true, true, false, false, true, true},   // 4
    {true, false, true, true, false, true, true},    // 5
    {true, false, true, true, true, true, true},     // 6
    {true, true, true, false, false, false, false},  // 7
    {true, true, true, true, true, true, true},      // 8
    {true, true, true, true, false, true, true},     // 9
};

// CPU counterpart to basic.frag's terrain value noise. Keeping this in
// lockstep with the shader lets decorative pebbles use the same wandering
// gravel boundary that is actually visible on the ground.
float terrainHash(glm::vec2 p) {
    p = glm::fract(p * glm::vec2(123.34f, 456.21f));
    p += glm::dot(p, p + 45.32f);
    return glm::fract(p.x * p.y);
}

float terrainValueNoise(glm::vec2 p) {
    glm::vec2 i = glm::floor(p);
    glm::vec2 f = glm::fract(p);
    float a = terrainHash(i);
    float b = terrainHash(i + glm::vec2(1.0f, 0.0f));
    float c = terrainHash(i + glm::vec2(0.0f, 1.0f));
    float d = terrainHash(i + glm::vec2(1.0f, 1.0f));
    glm::vec2 u = f * f * (3.0f - 2.0f * f);
    return glm::mix(glm::mix(a, b, u.x), glm::mix(c, d, u.x), u.y);
}

std::vector<uint8_t> generateTerrainControlPixels(uint32_t size, float worldSize) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
    auto encodeUNorm = [](float value) {
        return static_cast<uint8_t>(
            std::lround(glm::clamp(value, 0.0f, 1.0f) * 255.0f));
    };

    // Texel centres span the terrain's exact [-worldSize/2, worldSize/2]
    // range. basic.frag applies the matching half-texel UV inset when it
    // samples this clamped texture.
    for (uint32_t y = 0; y < size; ++y) {
        float worldZ = (static_cast<float>(y) / static_cast<float>(size - 1) - 0.5f) * worldSize;
        for (uint32_t x = 0; x < size; ++x) {
            float worldX =
                (static_cast<float>(x) / static_cast<float>(size - 1) - 0.5f) * worldSize;
            glm::vec2 worldXZ(worldX, worldZ);
            float warpX = terrainValueNoise(worldXZ * 0.015f + glm::vec2(5.2f, 88.1f));
            float warpY = terrainValueNoise(worldXZ * 0.017f + glm::vec2(41.7f, 12.3f));
            float grassPatch =
                terrainValueNoise(worldXZ * 0.06f + glm::vec2(19.3f, 4.7f)) * 0.7f +
                terrainValueNoise(worldXZ * 0.15f + glm::vec2(58.1f, 91.4f)) * 0.3f;
            float gravelPatch =
                terrainValueNoise(worldXZ * 0.08f + glm::vec2(71.2f, 33.6f)) * 0.7f +
                terrainValueNoise(worldXZ * 0.2f + glm::vec2(12.9f, 47.5f)) * 0.3f;

            size_t offset = (static_cast<size_t>(y) * size + x) * 4;
            pixels[offset] = encodeUNorm(warpX);
            pixels[offset + 1] = encodeUNorm(warpY);
            pixels[offset + 2] = encodeUNorm(grassPatch);
            pixels[offset + 3] = encodeUNorm(gravelPatch);
        }
    }
    return pixels;
}

float terrainGravelAmount(const Terrain& terrain, float x, float z) {
    constexpr float kRockyBaseHeight = -1.7f;  // matches basic.frag
    glm::vec2 worldXZ(x, z);
    float threshold = kRockyBaseHeight +
                      (terrainValueNoise(worldXZ * 0.05f + glm::vec2(153.2f, 88.7f)) -
                       0.5f) *
                          1.05f;
    float grassPatch = terrainValueNoise(worldXZ * 0.06f + glm::vec2(19.3f, 4.7f)) * 0.7f +
                       terrainValueNoise(worldXZ * 0.15f + glm::vec2(58.1f, 91.4f)) * 0.3f;
    float gravelPatch = terrainValueNoise(worldXZ * 0.08f + glm::vec2(71.2f, 33.6f)) * 0.7f +
                        terrainValueNoise(worldXZ * 0.2f + glm::vec2(12.9f, 47.5f)) * 0.3f;
    float boundaryBreakup = (grassPatch - gravelPatch) * 0.28f;
    float rockyBoundary = threshold - (terrain.heightAt(x, z) + boundaryBreakup);
    glm::vec3 terrainNormal = terrain.normalAt(x, z);
    float terrainSlope = glm::length(glm::vec2(terrainNormal.x, terrainNormal.z)) /
                         std::max(std::abs(terrainNormal.y), 0.15f);
    float physicalBlendWidth = glm::clamp(terrainSlope * 0.9f, 0.035f, 0.14f);
    float blendCoverage = glm::smoothstep(-physicalBlendWidth, physicalBlendWidth, rockyBoundary);
    float materialPattern =
        0.1f + terrainValueNoise(worldXZ * 0.9f + glm::vec2(37.1f, 214.6f)) * 0.8f;
    float heightRockiness =
        glm::smoothstep(materialPattern - 0.025f, materialPattern + 0.025f, blendCoverage);
    float steepness = 1.0f - terrainNormal.y;
    float slopeRockiness = glm::smoothstep(0.32f, 0.62f, steepness);
    return std::max(heightRockiness, slopeRockiness);
}

void addDigit(HudRenderer& hud, glm::vec2 centerNDC, float halfHeight, float aspect, int digit,
              glm::vec3 color) {
    if (digit < 0 || digit > 9) return;
    const bool* seg = kSevenSegmentTable[digit];

    float halfWidth = halfHeight * 0.5f / aspect;
    float thicknessX = kDigitSegmentThickness / aspect;
    float thicknessY = kDigitSegmentThickness;
    float armHalfHeight = halfHeight * 0.5f - thicknessY;

    if (seg[0]) hud.addQuad(centerNDC + glm::vec2(0.0f, halfHeight), {halfWidth, thicknessY}, color);
    if (seg[1]) hud.addQuad(centerNDC + glm::vec2(halfWidth, halfHeight * 0.5f), {thicknessX, armHalfHeight}, color);
    if (seg[2]) hud.addQuad(centerNDC + glm::vec2(halfWidth, -halfHeight * 0.5f), {thicknessX, armHalfHeight}, color);
    if (seg[3]) hud.addQuad(centerNDC + glm::vec2(0.0f, -halfHeight), {halfWidth, thicknessY}, color);
    if (seg[4]) hud.addQuad(centerNDC + glm::vec2(-halfWidth, -halfHeight * 0.5f), {thicknessX, armHalfHeight}, color);
    if (seg[5]) hud.addQuad(centerNDC + glm::vec2(-halfWidth, halfHeight * 0.5f), {thicknessX, armHalfHeight}, color);
    if (seg[6]) hud.addQuad(centerNDC, {halfWidth, thicknessY}, color);
}

// Lays digits out right-to-left from rightEdgeNDC, so the counter's right
// edge stays fixed and it grows leftward as the value gains digits (e.g.
// "9" -> "10"), which is what you want anchored to a screen corner.
void addNumber(HudRenderer& hud, int value, glm::vec2 rightEdgeNDC, float halfHeight, float aspect,
               glm::vec3 color) {
    std::string digits = std::to_string(std::max(0, value));
    float halfWidth = halfHeight * 0.5f / aspect;
    float advance = halfWidth * 2.4f;

    float x = rightEdgeNDC.x - halfWidth;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        addDigit(hud, {x, rightEdgeNDC.y}, halfHeight, aspect, *it - '0', color);
        x -= advance;
    }
}

VkImageMemoryBarrier2 imageBarrier(VkImage image, VkImageAspectFlags aspect,
                                    VkImageLayout oldLayout, VkImageLayout newLayout,
                                    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};
    return barrier;
}

}  // namespace

Application::Application(std::optional<ScreenshotRequest> screenshotRequest)
    : screenshotRequest_(std::move(screenshotRequest)) {
    initWindow();
    context_ = std::make_unique<VulkanContext>(window_);

    VkPhysicalDeviceProperties physicalDeviceProperties{};
    vkGetPhysicalDeviceProperties(context_->physicalDevice(), &physicalDeviceProperties);
    gpuTimestampPeriodNs_ = physicalDeviceProperties.limits.timestampPeriod;
    VkQueryPoolCreateInfo timestampPoolInfo{};
    timestampPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    timestampPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    timestampPoolInfo.queryCount =
        kGpuTimestampsPerFrame * CommandContext::kFramesInFlight;
    VK_CHECK(vkCreateQueryPool(context_->device(), &timestampPoolInfo, nullptr,
                               &gpuTimestampPool_));

    swapchain_ = std::make_unique<Swapchain>(*context_, window_);
    commands_ = std::make_unique<CommandContext>(*context_);
    historyBuffer_ = std::make_unique<HistoryBuffer>(*context_, *commands_, swapchain_->extent());
    pipeline_ = std::make_unique<Pipeline>(*context_, swapchain_->imageFormat(),
                                            swapchain_->depthFormat(), historyBuffer_->format());
    hud_ = std::make_unique<HudRenderer>(*context_, swapchain_->imageFormat(),
                                          swapchain_->depthFormat());

    // Each frame-in-flight slot reads the OTHER slot's history image (last
    // frame's temporally-accumulated result); like the TLAS descriptor,
    // this only needs writing once since the image views are stable until
    // a resize recreates them.
    for (size_t i = 0; i < CommandContext::kFramesInFlight; ++i) {
        size_t readSlot = 1 - i;
        pipeline_->updateHistoryDescriptor(i, historyBuffer_->imageView(readSlot),
                                            historyBuffer_->sampler());
    }

    // Both generators now offer 4 palette variants each (see
    // GrassTextureGenerator/RockTextureGenerator); basic.frag's patch-blend
    // painting only supports exactly one A/B pair per material, so rather
    // than picking a fixed pair, randomly choose 2 distinct variants each
    // run -- more variety across playthroughs without touching that
    // blending logic.
    std::mt19937 textureVariantRng(std::random_device{}());
    auto pickTwoDistinct = [&](int count) {
        std::uniform_int_distribution<int> dist(0, count - 1);
        int a = dist(textureVariantRng);
        int b = dist(textureVariantRng);
        if (b == a) b = (b + 1) % count;
        return std::pair<int, int>(a, b);
    };
    auto [grassVariantA, grassVariantB] = pickTwoDistinct(4);
    auto [rockVariantA, rockVariantB] = pickTwoDistinct(4);

    constexpr uint32_t kTerrainTextureRes = 512;
    std::vector<uint8_t> grassPixelsA = GrassTextureGenerator::generate(kTerrainTextureRes, grassVariantA);
    grassTextureA_ = std::make_unique<Texture>(Texture::fromPixels(
        *context_, *commands_, kTerrainTextureRes, kTerrainTextureRes, grassPixelsA, /*repeat=*/true));
    std::vector<uint8_t> grassPixelsB = GrassTextureGenerator::generate(kTerrainTextureRes, grassVariantB);
    grassTextureB_ = std::make_unique<Texture>(Texture::fromPixels(
        *context_, *commands_, kTerrainTextureRes, kTerrainTextureRes, grassPixelsB, /*repeat=*/true));
    std::vector<uint8_t> rockPixelsA = RockTextureGenerator::generate(kTerrainTextureRes, rockVariantA);
    rockTextureA_ = std::make_unique<Texture>(Texture::fromPixels(
        *context_, *commands_, kTerrainTextureRes, kTerrainTextureRes, rockPixelsA, /*repeat=*/true));
    std::vector<uint8_t> rockPixelsB = RockTextureGenerator::generate(kTerrainTextureRes, rockVariantB);
    rockTextureB_ = std::make_unique<Texture>(Texture::fromPixels(
        *context_, *commands_, kTerrainTextureRes, kTerrainTextureRes, rockPixelsB, /*repeat=*/true));
    std::vector<uint8_t> terrainControlPixels =
        generateTerrainControlPixels(kTerrainTextureRes, /*worldSize=*/180.0f);
    terrainControlTexture_ = std::make_unique<Texture>(Texture::fromPixels(
        *context_, *commands_, kTerrainTextureRes, kTerrainTextureRes, terrainControlPixels,
        /*repeat=*/false));
    std::vector<uint8_t> trackPixels = TrackTextureGenerator::generate(128);
    trackTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 128, 128, trackPixels, /*repeat=*/false));
    std::vector<uint8_t> cloudPixels = CloudTextureGenerator::generate(256);
    // The dome's UV projects onto a distant horizontal plane and can run
    // well outside [0,1] near the horizon, so this needs to tile.
    cloudTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 256, 256, cloudPixels, /*repeat=*/true));
    std::vector<uint8_t> cratePixels = CrateTextureGenerator::generate(128);
    // Mapped exactly once per cube face (see Mesh::cube's UV), not tiled,
    // so CLAMP rather than REPEAT.
    crateTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 128, 128, cratePixels, /*repeat=*/false));
    std::vector<uint8_t> whitePixel = {255, 255, 255, 255};
    whiteTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 1, 1, whitePixel, /*repeat=*/false));
    // Tiles since the tank model's own UV layout isn't a single clean 0..1
    // island per part -- see CamoTextureGenerator.
    std::vector<uint8_t> camoPixels = CamoTextureGenerator::generate(256);
    camoTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 256, 256, camoPixels, /*repeat=*/true));
    std::vector<uint8_t> metalPixels = MetalTextureGenerator::generate(128);
    metalTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 128, 128, metalPixels, /*repeat=*/true));
    // Both boundary textures tile along the perimeter's length (see
    // BoundaryGenerator's UVs), so repeat=true.
    std::vector<uint8_t> boundaryLinePixels = BoundaryTextureGenerator::generateGroundLine(128);
    boundaryLineTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 128, 128, boundaryLinePixels, /*repeat=*/true));
    std::vector<uint8_t> boundaryWallPixels = BoundaryTextureGenerator::generateWall(128);
    boundaryWallTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 128, 128, boundaryWallPixels, /*repeat=*/true));
    // Terrain patch-blends grass A/B and gravel A/B, then blends that by
    // height (see heightBlend in PushConstants/basic.frag), using the fifth
    // control texture for its pre-baked UV warp and patch masks. Everything
    // else binds a single texture into the albedo slots and lets Pipeline
    // supply that same texture as the unused control-map fallback.
    terrainMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*grassTextureA_, *grassTextureB_,
                                                                    *rockTextureA_, *rockTextureB_,
                                                                    terrainControlTexture_.get());
    trackMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*trackTexture_, *trackTexture_,
                                                                  *trackTexture_, *trackTexture_);
    cloudMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*cloudTexture_, *cloudTexture_,
                                                                  *cloudTexture_, *cloudTexture_);
    // Bark/leaf/standalone-rock material sets are allocated per mesh
    // variant instead, alongside their textures -- see the tree/rock mesh
    // construction loops below (barkMaterialSets_/leafMaterialSets_/
    // rockMaterialSets_).
    crateMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*crateTexture_, *crateTexture_,
                                                                  *crateTexture_, *crateTexture_);
    whiteMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*whiteTexture_, *whiteTexture_,
                                                                  *whiteTexture_, *whiteTexture_);
    camoMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*camoTexture_, *camoTexture_,
                                                                 *camoTexture_, *camoTexture_);
    metalMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*metalTexture_, *metalTexture_,
                                                                  *metalTexture_, *metalTexture_);
    boundaryLineMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(
        *boundaryLineTexture_, *boundaryLineTexture_, *boundaryLineTexture_, *boundaryLineTexture_);
    boundaryWallMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(
        *boundaryWallTexture_, *boundaryWallTexture_, *boundaryWallTexture_, *boundaryWallTexture_);

    uint32_t terrainSeed = std::random_device{}();
    // 256, not the old 64 -> 128 -> 256 progression: coarser hills' large
    // flat triangles were visible as faceting at grazing angles/close
    // range, and the sharper features HeightmapGenerator now carves (the
    // plateau's edge, the river's banks) need considerably more grid
    // resolution than gentle sine-wave hills ever did to read as an actual
    // slope instead of a single blocky triangle strip. Still cheap for
    // hardware ray tracing either way -- (256-1)^2*2 ~= 130k triangles for
    // the whole terrain BLAS, built once at load time.
    terrain_ = std::make_unique<Terrain>(*context_, *commands_, /*resolution=*/256,
                                          /*worldSize=*/180.0f, /*amplitude=*/2.2f, terrainSeed);
    WaterGenerator::FloodField waterField =
        WaterGenerator::computeFloodField(*terrain_, kWaterThreshold, kWaterMaxDepth);
    waterMesh_ = WaterGenerator::buildMesh(*context_, *commands_, *terrain_, waterField);
    // The boundary sits kBoundaryInsetFraction of the terrain's total width
    // in from its actual edge, forming a smaller square play area.
    boundaryHalfExtent_ = terrain_->worldSize() * (0.5f - kBoundaryInsetFraction);
    boundaryLineMesh_ =
        BoundaryGenerator::buildLineMesh(*context_, *commands_, *terrain_, boundaryHalfExtent_);
    boundaryWallMesh_ = BoundaryGenerator::buildWallMesh(*context_, *commands_, *terrain_,
                                                          boundaryHalfExtent_, kBoundaryWallHeight);
    tank_ = std::make_unique<Tank>(*context_, *commands_,
                                    std::string(ASSET_ROOT) + "/assets/models/tank.x");
    // Near-white so the crate texture's own wood color/detail shows through
    // unmodified (same reasoning as the bark/leaf/rock tints).
    boxMesh_ = std::make_unique<Mesh>(Mesh::cube(*context_, *commands_, glm::vec3(1.0f)));
    // Muted brass/gunmetal, not the old placeholder cube's bright yellow --
    // reads as an actual metal shell casing under the specularStrength this
    // gets in the draw loop below.
    shellMesh_ =
        std::make_unique<Mesh>(Mesh::shell(*context_, *commands_, glm::vec3(0.58f, 0.52f, 0.4f)));
    // An irregular blob rather than a literal flat-faced cube -- reads as
    // an actual fireball/burst instead of a scaling box.
    flashMesh_ =
        std::make_unique<Mesh>(Mesh::blobCluster(*context_, *commands_, glm::vec3(1.0f, 0.85f, 0.55f)));
    // Explosion debris: a darker, splintered-looking chunk of the box
    // (normally lit, so it tumbles through the scene's light/shadow like
    // real debris) and a small, bright unlit ember (a spark/fire glow that
    // ignores lighting entirely) -- see DebrisParticle and spawnExplosion.
    debrisChunkMesh_ =
        std::make_unique<Mesh>(Mesh::cube(*context_, *commands_, glm::vec3(0.32f, 0.22f, 0.12f)));
    debrisEmberMesh_ =
        std::make_unique<Mesh>(Mesh::cube(*context_, *commands_, glm::vec3(1.0f, 0.55f, 0.1f)));
    // Neutral grey, drawn unlit and alpha-blended (see the smokePuffs_ draw
    // loop) -- muzzle blast and shell-trail wisps, see SmokePuff.h.
    smokePuffMesh_ =
        std::make_unique<Mesh>(Mesh::blobCluster(*context_, *commands_, glm::vec3(0.5f, 0.5f, 0.5f)));
    // Track dust shares SmokePuff's update/draw path, but needs an earthy
    // vertex tint rather than muzzle smoke's neutral grey.
    dustPuffMesh_ = std::make_unique<Mesh>(
        Mesh::blobCluster(*context_, *commands_, glm::vec3(0.32f, 0.23f, 0.13f)));
    // White so the track texture's own baked-in brown color shows through
    // unmodified (same reasoning as terrain's kTerrainColor).
    trackMarkMesh_ = std::make_unique<Mesh>(Mesh::quad(*context_, *commands_, glm::vec3(1.0f)));
    // A handful of distinct rock shapes/tints (see Mesh::rock), reused
    // across many cluster instances via meshVariant rather than generating
    // unique geometry per rock. Rocks now sample an actual gravel texture
    // (rockMaterialSets_) -- a different RockTextureGenerator variant per
    // shape (cycling through its 4 palettes) plus each one's own tint, so
    // the pool reads as genuinely different-looking boulders rather than
    // one gravel texture in five brightness levels.
    const glm::vec3 rockShades[] = {
        {1.0f, 0.98f, 0.95f}, {0.85f, 0.83f, 0.80f}, {1.05f, 1.0f, 0.92f},
        {0.90f, 0.90f, 0.90f}, {1.05f, 0.95f, 0.82f},
    };
    for (size_t i = 0; i < sizeof(rockShades) / sizeof(rockShades[0]); ++i) {
        rockMeshes_.push_back(std::make_unique<Mesh>(
            Mesh::rock(*context_, *commands_, rockShades[i], static_cast<uint32_t>(i) + 1)));
        mediumRockMeshes_.push_back(std::make_unique<Mesh>(Mesh::rock(
            *context_, *commands_, rockShades[i], static_cast<uint32_t>(i) + 1,
            /*subdivisions=*/2)));
        smallRockMeshes_.push_back(std::make_unique<Mesh>(Mesh::rock(
            *context_, *commands_, rockShades[i], static_cast<uint32_t>(i) + 1,
            /*subdivisions=*/1)));
        rockProxyMeshes_.push_back(std::make_unique<Mesh>(Mesh::rock(
            *context_, *commands_, rockShades[i], static_cast<uint32_t>(i) + 1,
            /*subdivisions=*/1, /*radiusScale=*/0.78f)));

        std::vector<uint8_t> rockPixels = RockTextureGenerator::generate(128, static_cast<uint32_t>(i));
        rockStandaloneTextures_.push_back(std::make_unique<Texture>(
            Texture::fromPixels(*context_, *commands_, 128, 128, rockPixels, /*repeat=*/true)));
        rockMaterialSets_.push_back(pipeline_->allocateMaterialDescriptorSet(
            *rockStandaloneTextures_.back(), *rockStandaloneTextures_.back(),
            *rockStandaloneTextures_.back(), *rockStandaloneTextures_.back()));
    }
    sedimentaryCliffMesh_ = std::make_unique<Mesh>(Mesh::sedimentaryCliff(
        *context_, *commands_, glm::vec3(1.0f, 1.0f, 1.0f), 7331));
    sedimentaryCliffGrassMesh_ = std::make_unique<Mesh>(Mesh::sedimentaryCliff(
        *context_, *commands_, glm::vec3(1.0f), 7331, /*topOnly=*/true));
    // A small pool of distinct fractal branch structures (see
    // Mesh::treeBark/treeLeaves) -- matching seeds so each variant's bark
    // and leaves share the same branch skeleton. Tints kept close to white
    // since bark/leaf color comes from a different BarkTextureGenerator/
    // LeafTextureGenerator palette per variant instead (barkMaterialSets_/
    // leafMaterialSets_), so the pool reads as genuinely different trees.
    constexpr int kTreeVariantCount = 4;
    const glm::vec3 barkTint(0.95f, 0.92f, 0.88f);
    const glm::vec3 leafTint(0.92f, 1.0f, 0.88f);
    for (int i = 0; i < kTreeVariantCount; ++i) {
        uint32_t seed = static_cast<uint32_t>(i) + 1;
        treeBarkMeshes_.push_back(
            std::make_unique<Mesh>(Mesh::treeBark(*context_, *commands_, barkTint, seed)));
        treeLeafMeshes_.push_back(
            std::make_unique<Mesh>(Mesh::treeLeaves(*context_, *commands_, leafTint, seed)));
        mediumTreeBarkMeshes_.push_back(
            std::make_unique<Mesh>(Mesh::treeBark(*context_, *commands_, barkTint, seed, 1)));
        mediumTreeLeafMeshes_.push_back(
            std::make_unique<Mesh>(Mesh::treeLeaves(*context_, *commands_, leafTint, seed, 1)));
        farTreeBarkMeshes_.push_back(
            std::make_unique<Mesh>(Mesh::treeBark(*context_, *commands_, barkTint, seed, 2)));
        farTreeLeafMeshes_.push_back(
            std::make_unique<Mesh>(Mesh::treeLeaves(*context_, *commands_, leafTint, seed, 2)));
        treeLeafProxyMeshes_.push_back(
            std::make_unique<Mesh>(Mesh::treeLeaves(*context_, *commands_, leafTint, seed, 3)));

        std::vector<uint8_t> barkPixels = BarkTextureGenerator::generate(128, static_cast<uint32_t>(i));
        barkTextures_.push_back(std::make_unique<Texture>(
            Texture::fromPixels(*context_, *commands_, 128, 128, barkPixels, /*repeat=*/true)));
        barkMaterialSets_.push_back(pipeline_->allocateMaterialDescriptorSet(
            *barkTextures_.back(), *barkTextures_.back(), *barkTextures_.back(), *barkTextures_.back()));

        std::vector<uint8_t> leafPixels = LeafTextureGenerator::generate(128, static_cast<uint32_t>(i));
        leafTextures_.push_back(std::make_unique<Texture>(
            Texture::fromPixels(*context_, *commands_, 128, 128, leafPixels, /*repeat=*/true)));
        leafMaterialSets_.push_back(pipeline_->allocateMaterialDescriptorSet(
            *leafTextures_.back(), *leafTextures_.back(), *leafTextures_.back(), *leafTextures_.back()));
    }
    // Small bushes -- reuse the same leaf textures/material sets as tree
    // foliage (leafMaterialSets_ above) rather than a texture pool of their
    // own; a shrub is basically "foliage clump with no trunk", so the same
    // material reads fine on it. One shrub mesh variant per leaf texture
    // variant, drawn by matching index (see the shrubs_ draw loop).
    for (int i = 0; i < kTreeVariantCount; ++i) {
        shrubMeshes_.push_back(std::make_unique<Mesh>(
            Mesh::shrub(*context_, *commands_, leafTint, static_cast<uint32_t>(i) + 101)));
    }
    // White so the cloud texture's own baked-in white/grey shading shows
    // through unmodified; uvScale tuned by eye for plausible-looking cloud
    // size once projected onto the dome's "distant plane" mapping.
    cloudDomeMesh_ =
        std::make_unique<Mesh>(Mesh::dome(*context_, *commands_, glm::vec3(1.0f), 0.25f));
    spawnBoxes();
    spawnTrees(waterField);
    spawnRocks(waterField);
    spawnSedimentaryCliffs(waterField);
    spawnShrubs(waterField);
    spawnSmallRocks(waterField);

    // Static collision circles for the tank's own movement (see
    // Tank::update) -- trees/rocks never move, so this is built once
    // rather than every frame. Radii are rough stand-ins for a trunk
    // (trees) or the rock's own jittered-icosahedron footprint (rocks),
    // scaled by each instance's own scale.
    for (const auto& tree : trees_) {
        obstacles_.push_back(
            {glm::vec2(tree.position.x, tree.position.z), 0.4f * tree.scale});
    }
    for (const auto& rock : rocks_) {
        obstacles_.push_back(
            {glm::vec2(rock.position.x, rock.position.z), 0.8f * rock.scale});
    }
    for (const auto& cliff : sedimentaryCliffs_) {
        // Each independently terrain-fitted section is approximated by a
        // short capsule-like chain rather than the old formation-wide one.
        // CollisionSystem is 2D, so explicitly test how much stone is above
        // the terrain at each circle: a buried/flush plate should not become
        // an invisible wall merely because its mesh extends underground.
        constexpr int kCliffCollisionPieces = 5;
        constexpr float kCliffHalfLength = 4.4f;
        constexpr float kCliffTopLocalHeight = 0.5f;  // includes the turf cap
        constexpr float kMinimumBlockingExposure = 0.24f;
        constexpr float kFullBlockingExposure = 0.65f;
        constexpr float kMinPieceRadius = 0.55f;
        constexpr float kMaxPieceRadius = 1.2f;
        glm::vec2 localXAxis(std::cos(cliff.yaw), -std::sin(cliff.yaw));
        glm::vec2 cliffCenter(cliff.position.x, cliff.position.z);
        for (int piece = 0; piece < kCliffCollisionPieces; ++piece) {
            float t = static_cast<float>(piece) / (kCliffCollisionPieces - 1);
            float localX = glm::mix(-kCliffHalfLength, kCliffHalfLength, t);
            glm::vec2 center = cliffCenter + localXAxis * (localX * cliff.scale);
            float visibleTop = cliff.position.y + kCliffTopLocalHeight * cliff.scale;
            float exposedHeight = visibleTop - terrain_->heightAt(center.x, center.y);
            if (exposedHeight <= kMinimumBlockingExposure) continue;

            float exposure = glm::smoothstep(kMinimumBlockingExposure,
                                             kFullBlockingExposure, exposedHeight);
            float radius = glm::mix(kMinPieceRadius, kMaxPieceRadius, exposure) * cliff.scale;
            obstacles_.push_back({center, radius});
        }
    }

    buildAccelerationStructures();
    input_ = std::make_unique<InputManager>(window_);
    lastFrameTime_ = glfwGetTime();
}

Application::~Application() {
    if (context_) vkDeviceWaitIdle(context_->device());

    if (context_ && gpuTimestampPool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(context_->device(), gpuTimestampPool_, nullptr);
        gpuTimestampPool_ = VK_NULL_HANDLE;
    }

    // Destroy in dependency order before the GLFW window disappears.
    historyBuffer_.reset();
    sceneAS_.reset();
    sedimentaryCliffBLAS_.reset();
    treeLeafBLAS_.clear();
    treeBarkBLAS_.clear();
    rockBLAS_.clear();
    shellBLAS_.reset();
    boxBLAS_.reset();
    whiteTexture_.reset();
    metalTexture_.reset();
    camoTexture_.reset();
    boundaryWallTexture_.reset();
    boundaryLineTexture_.reset();
    crateTexture_.reset();
    leafTextures_.clear();
    barkTextures_.clear();
    rockStandaloneTextures_.clear();
    cloudTexture_.reset();
    trackTexture_.reset();
    terrainControlTexture_.reset();
    rockTextureB_.reset();
    rockTextureA_.reset();
    grassTextureB_.reset();
    grassTextureA_.reset();
    hud_.reset();
    cloudDomeMesh_.reset();
    boundaryWallMesh_.reset();
    boundaryLineMesh_.reset();
    waterMesh_.reset();
    sedimentaryCliffGrassMesh_.reset();
    sedimentaryCliffMesh_.reset();
    rockProxyMeshes_.clear();
    smallRockMeshes_.clear();
    mediumRockMeshes_.clear();
    rockMeshes_.clear();
    farTreeLeafMeshes_.clear();
    farTreeBarkMeshes_.clear();
    treeLeafProxyMeshes_.clear();
    mediumTreeLeafMeshes_.clear();
    mediumTreeBarkMeshes_.clear();
    treeLeafMeshes_.clear();
    treeBarkMeshes_.clear();
    shrubMeshes_.clear();
    trackMarkMesh_.reset();
    debrisEmberMesh_.reset();
    debrisChunkMesh_.reset();
    dustPuffMesh_.reset();
    smokePuffMesh_.reset();
    flashMesh_.reset();
    shellMesh_.reset();
    boxMesh_.reset();
    tank_.reset();
    terrain_.reset();
    pipeline_.reset();
    commands_.reset();
    swapchain_.reset();
    context_.reset();

    if (window_) {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }
}

void Application::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(kWindowWidth, kWindowHeight, "tanks", nullptr, nullptr);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);
}

void Application::framebufferResizeCallback(GLFWwindow* window, int /*width*/, int /*height*/) {
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
    app->framebufferResized_ = true;
}

void Application::run() { mainLoop(); }

void Application::mainLoop() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }

        double now = glfwGetTime();
        float deltaTime = static_cast<float>(now - lastFrameTime_);
        lastFrameTime_ = now;
        // Exponential moving average rather than the raw instantaneous
        // value, which jitters wildly frame to frame and is unreadable as
        // an on-screen counter.
        if (deltaTime > 0.0f) fpsSmoothed_ = glm::mix(fpsSmoothed_, 1.0f / deltaTime, 0.1f);

        input_->update();

        bool fDown = glfwGetKey(window_, GLFW_KEY_F) == GLFW_PRESS;
        if (fDown && !prevFKeyDown_) followTank_ = !followTank_;
        prevFKeyDown_ = fDown;

        // Manual screenshot capture, saved via the same GPU-readback path
        // as the --screenshot CLI flag (see drawFrame) rather than any
        // OS-level screenshot tool -- see ScreenshotRequest's comment for
        // why. Only arms a new request if one isn't already pending, so
        // holding the key doesn't queue up a burst of captures.
        bool screenshotKeyDown = glfwGetKey(window_, GLFW_KEY_F12) == GLFW_PRESS;
        if (screenshotKeyDown && !prevScreenshotKeyDown_ && !screenshotRequest_) {
            screenshotRequest_ = ScreenshotRequest{nextScreenshotPath(), frameCounter_ + 1, false};
        }
        prevScreenshotKeyDown_ = screenshotKeyDown;

        tank_->update(*input_, deltaTime, *terrain_, obstacles_, boundaryHalfExtent_);
        updateTrackMarks(deltaTime);

        bool fireDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ||
                        glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (fireDown && !prevFireDown_) fireProjectile();
        prevFireDown_ = fireDown;

        updateProjectilesAndCollisions(deltaTime);

        if (followTank_) {
            camera_.followTarget(tank_->position(), tank_->forward());
        } else {
            camera_.update(*input_, deltaTime);
        }

        drawFrame();
    }
}

void Application::spawnBoxes() {
    constexpr int kBoxCount = 8;
    constexpr float kEdgeMargin = 6.0f;  // keep boxes off the play-area boundary's wall of light
    constexpr float kMinDistanceFromSpawn = 10.0f;  // tank starts at the origin
    constexpr float kMinDistanceBetweenBoxes = 6.0f;
    constexpr int kMaxAttemptsPerBox = 50;

    std::mt19937 rng(std::random_device{}());
    float half = boundaryHalfExtent_ - kEdgeMargin;
    std::uniform_real_distribution<float> coordDist(-half, half);
    std::uniform_real_distribution<float> yawDist(0.0f, 6.2831853f);

    std::vector<glm::vec2> placed;
    for (int i = 0; i < kBoxCount; ++i) {
        glm::vec2 pos{0.0f, 0.0f};
        for (int attempt = 0; attempt < kMaxAttemptsPerBox; ++attempt) {
            glm::vec2 candidate(coordDist(rng), coordDist(rng));
            bool tooCloseToSpawn = glm::length(candidate) < kMinDistanceFromSpawn;
            bool tooCloseToOther =
                std::any_of(placed.begin(), placed.end(), [&](glm::vec2 p) {
                    return glm::length(p - candidate) < kMinDistanceBetweenBoxes;
                });
            pos = candidate;
            if (!tooCloseToSpawn && !tooCloseToOther) break;
        }
        placed.push_back(pos);

        Box box;
        box.size = 2.0f;
        box.position = glm::vec3(pos.x, terrain_->heightAt(pos.x, pos.y) + box.size * 0.5f, pos.y);
        box.up = terrain_->normalAt(pos.x, pos.y);
        box.yaw = yawDist(rng);
        boxes_.push_back(box);
    }
}

void Application::spawnTrees(const WaterGenerator::FloodField& waterField) {
    constexpr int kTreeCount = 100;
    constexpr float kEdgeMargin = 3.0f;
    constexpr float kMinDistanceFromSpawn = 8.0f;
    constexpr float kMinDistanceBetweenTrees = 3.0f;
    constexpr int kMaxAttemptsPerTree = 30;

    std::mt19937 rng(std::random_device{}());
    float half = terrain_->worldSize() * 0.5f - kEdgeMargin;
    std::uniform_real_distribution<float> coordDist(-half, half);
    std::uniform_real_distribution<float> yawDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> scaleDist(0.8f, 1.4f);
    std::uniform_int_distribution<int> variantDist(0, static_cast<int>(treeBarkMeshes_.size()) - 1);

    std::vector<glm::vec2> placed;
    for (int i = 0; i < kTreeCount; ++i) {
        glm::vec2 pos{0.0f, 0.0f};
        for (int attempt = 0; attempt < kMaxAttemptsPerTree; ++attempt) {
            glm::vec2 candidate(coordDist(rng), coordDist(rng));
            bool tooCloseToSpawn = glm::length(candidate) < kMinDistanceFromSpawn;
            bool tooCloseToOther = std::any_of(placed.begin(), placed.end(), [&](glm::vec2 p) {
                return glm::length(p - candidate) < kMinDistanceBetweenTrees;
            });
            bool underwater = WaterGenerator::isUnderwater(waterField, candidate.x, candidate.y);
            pos = candidate;
            if (!tooCloseToSpawn && !tooCloseToOther && !underwater) break;
        }
        placed.push_back(pos);

        TreeInstance tree;
        tree.position = glm::vec3(pos.x, terrain_->heightAt(pos.x, pos.y), pos.y);
        tree.yaw = yawDist(rng);
        tree.scale = scaleDist(rng);
        tree.meshVariant = variantDist(rng);
        trees_.push_back(tree);
    }
}

void Application::spawnRocks(const WaterGenerator::FloodField& waterField) {
    // Cluster/rock counts kept in check against the scene's TLAS instance
    // capacity (SceneAccelerationStructure::kMaxInstances = 384): worst case
    // here is 16*6=96 rocks; combined with trees (100*2=200 instances, bark
    // + leaves each) and the terrain/tank/boxes/shells baseline (well under
    // 30), that's ~326 worst case, leaving headroom under the cap.
    constexpr int kClusterCount = 16;
    constexpr float kEdgeMargin = 3.0f;
    constexpr float kMinDistanceFromSpawn = 6.0f;
    constexpr float kMinDistanceBetweenClusters = 6.0f;
    constexpr int kMaxAttemptsPerCluster = 30;
    constexpr int kMinRocksPerCluster = 3;
    constexpr int kMaxRocksPerCluster = 6;
    constexpr float kClusterRadius = 2.2f;
    constexpr float kEmbedDepth = 0.15f;  // sinks each rock in slightly so it reads as grounded, not floating

    std::mt19937 rng(std::random_device{}());
    float half = terrain_->worldSize() * 0.5f - kEdgeMargin;
    std::uniform_real_distribution<float> coordDist(-half, half);
    std::uniform_real_distribution<float> yawDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> scaleDist(0.5f, 1.3f);
    std::uniform_real_distribution<float> offsetDist(-kClusterRadius, kClusterRadius);
    std::uniform_int_distribution<int> countDist(kMinRocksPerCluster, kMaxRocksPerCluster);
    std::uniform_int_distribution<int> variantDist(0, static_cast<int>(rockMeshes_.size()) - 1);

    std::vector<glm::vec2> clusterCenters;
    for (int c = 0; c < kClusterCount; ++c) {
        glm::vec2 center{0.0f, 0.0f};
        for (int attempt = 0; attempt < kMaxAttemptsPerCluster; ++attempt) {
            glm::vec2 candidate(coordDist(rng), coordDist(rng));
            bool tooCloseToSpawn = glm::length(candidate) < kMinDistanceFromSpawn;
            bool tooCloseToOther =
                std::any_of(clusterCenters.begin(), clusterCenters.end(), [&](glm::vec2 p) {
                    return glm::length(p - candidate) < kMinDistanceBetweenClusters;
                });
            bool underwater = WaterGenerator::isUnderwater(waterField, candidate.x, candidate.y);
            center = candidate;
            if (!tooCloseToSpawn && !tooCloseToOther && !underwater) break;
        }
        clusterCenters.push_back(center);

        int rockCount = countDist(rng);
        for (int r = 0; r < rockCount; ++r) {
            glm::vec2 pos = center + glm::vec2(offsetDist(rng), offsetDist(rng));
            // Individual rocks within a cluster can still land in water even
            // when the cluster center didn't -- just skip that one rock
            // rather than rejecting/relocating the whole cluster.
            if (WaterGenerator::isUnderwater(waterField, pos.x, pos.y)) continue;

            RockInstance rock;
            rock.position =
                glm::vec3(pos.x, terrain_->heightAt(pos.x, pos.y) - kEmbedDepth, pos.y);
            rock.yaw = yawDist(rng);
            rock.scale = scaleDist(rng);
            rock.meshVariant = variantDist(rng);
            rocks_.push_back(rock);
        }
    }
}

void Application::spawnSedimentaryCliffs(const WaterGenerator::FloodField& waterField) {
    constexpr int kMaxFormations = 5;
    constexpr int kSectionsPerFormation = 5;
    constexpr float kSectionSpacing = 5.0f;
    constexpr float kGridStep = 3.0f;
    constexpr float kMinSeparation = 22.0f;
    constexpr float kEdgeMargin = 18.0f;
    constexpr float kSpawnClearRadius = 10.0f;

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> scaleDist(1.0625f, 1.5f);
    struct CliffCandidate {
        glm::vec2 position;
        float steepness;
    };
    std::vector<CliffCandidate> candidates;
    float half = terrain_->worldSize() * 0.5f - kEdgeMargin;
    for (float x = -half; x <= half; x += kGridStep) {
        for (float z = -half; z <= half; z += kGridStep) {
            glm::vec2 position(x, z);
            if (glm::length(position) < kSpawnClearRadius) continue;
            if (WaterGenerator::isUnderwater(waterField, x, z)) continue;
            float steepness = 1.0f - terrain_->normalAt(x, z).y;
            candidates.push_back({position, steepness});
        }
    }
    // The terrain seed changes its absolute slope range considerably, and
    // the deliberately flatter terrain can have no samples above a fixed
    // threshold. Ranking candidates makes these formations reliably follow
    // the steepest ground available on every generated map.
    std::shuffle(candidates.begin(), candidates.end(), rng);  // random tie-breaking
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.steepness > b.steepness;
    });

    std::vector<glm::vec2> placed;
    int formationsPlaced = 0;
    for (const CliffCandidate& candidate : candidates) {
        if (formationsPlaced >= kMaxFormations) break;
        glm::vec2 position = candidate.position;
        bool tooClose = std::any_of(placed.begin(), placed.end(), [&](glm::vec2 other) {
            return glm::length(position - other) < kMinSeparation;
        });
        if (tooClose) continue;

        float formationScale = scaleDist(rng);

        // Walk outward in both directions along the local contour. At every
        // step the tangent is recomputed from the terrain normal, allowing
        // the chain to bend with the hillside instead of remaining a rigid
        // straight slab. Preserve tangent direction from step to step so a
        // changing normal cannot suddenly reverse the walk.
        std::vector<glm::vec2> sectionPositions(kSectionsPerFormation);
        int middle = kSectionsPerFormation / 2;
        sectionPositions[middle] = position;
        auto contourTangent = [&](glm::vec2 p, glm::vec2 preferredDirection) {
            glm::vec3 n = terrain_->normalAt(p.x, p.y);
            glm::vec2 tangent(n.z, -n.x);
            float length = glm::length(tangent);
            if (length < 0.001f) tangent = preferredDirection;
            else tangent /= length;
            if (glm::dot(tangent, preferredDirection) < 0.0f) tangent = -tangent;
            return tangent;
        };

        glm::vec3 centerNormal = terrain_->normalAt(position.x, position.y);
        glm::vec2 initialTangent(centerNormal.z, -centerNormal.x);
        if (glm::length(initialTangent) < 0.001f) initialTangent = glm::vec2(1.0f, 0.0f);
        else initialTangent = glm::normalize(initialTangent);
        glm::vec2 direction = initialTangent;
        for (int section = middle + 1; section < kSectionsPerFormation; ++section) {
            direction = contourTangent(sectionPositions[section - 1], direction);
            sectionPositions[section] =
                sectionPositions[section - 1] + direction * (kSectionSpacing * formationScale);
        }
        direction = -initialTangent;
        for (int section = middle - 1; section >= 0; --section) {
            direction = contourTangent(sectionPositions[section + 1], direction);
            sectionPositions[section] =
                sectionPositions[section + 1] + direction * (kSectionSpacing * formationScale);
        }

        for (glm::vec2 sectionPosition : sectionPositions) {
            glm::vec3 normal = terrain_->normalAt(sectionPosition.x, sectionPosition.y);
            RockInstance cliff;
            cliff.position = {sectionPosition.x,
                              terrain_->heightAt(sectionPosition.x, sectionPosition.y) - 0.08f,
                              sectionPosition.y};
            cliff.yaw = std::atan2(normal.x, normal.z);
            cliff.scale = formationScale;
            sedimentaryCliffs_.push_back(cliff);
        }
        ++formationsPlaced;
        placed.push_back(position);
    }
}

void Application::spawnShrubs(const WaterGenerator::FloodField& waterField) {
    constexpr int kShrubCount = 140;
    constexpr float kEdgeMargin = 3.0f;
    constexpr float kMinDistanceFromSpawn = 6.0f;
    constexpr float kMinDistanceBetweenShrubs = 2.0f;
    constexpr int kMaxAttemptsPerShrub = 20;

    std::mt19937 rng(std::random_device{}());
    float half = terrain_->worldSize() * 0.5f - kEdgeMargin;
    std::uniform_real_distribution<float> coordDist(-half, half);
    std::uniform_real_distribution<float> yawDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> scaleDist(0.7f, 1.3f);
    std::uniform_int_distribution<int> variantDist(0, static_cast<int>(shrubMeshes_.size()) - 1);

    std::vector<glm::vec2> placed;
    for (int i = 0; i < kShrubCount; ++i) {
        glm::vec2 pos{0.0f, 0.0f};
        bool found = false;
        for (int attempt = 0; attempt < kMaxAttemptsPerShrub; ++attempt) {
            glm::vec2 candidate(coordDist(rng), coordDist(rng));
            bool tooCloseToSpawn = glm::length(candidate) < kMinDistanceFromSpawn;
            bool tooCloseToOther = std::any_of(placed.begin(), placed.end(), [&](glm::vec2 p) {
                return glm::length(p - candidate) < kMinDistanceBetweenShrubs;
            });
            bool underwater = WaterGenerator::isUnderwater(waterField, candidate.x, candidate.y);
            if (tooCloseToSpawn || tooCloseToOther || underwater) continue;
            pos = candidate;
            found = true;
            break;
        }
        // Unlike trees/rocks (which fall back to placing wherever the last
        // attempt landed), just skip this one -- a shrub or two short of
        // kShrubCount is invisible; forcing a placement that failed every
        // rejection check isn't worth the risk of landing in water.
        if (!found) continue;
        placed.push_back(pos);

        ShrubInstance shrub;
        shrub.position = glm::vec3(pos.x, terrain_->heightAt(pos.x, pos.y), pos.y);
        shrub.yaw = yawDist(rng);
        shrub.scale = scaleDist(rng);
        shrub.meshVariant = variantDist(rng);
        shrubs_.push_back(shrub);
    }
}

void Application::spawnSmallRocks(const WaterGenerator::FloodField& waterField) {
    // Small decorative scree/pebbles, concentrated wherever the rendered
    // terrain is gravel -- NOT
    // added to the ray-traced TLAS (see gatherRayTracingInstances' comment
    // on debris/track marks for the same reasoning: numerous and small
    // enough that individually shadow-casting each one isn't worth the TLAS
    // churn). That's what actually lets this be "lots" without threatening
    // SceneAccelerationStructure::kMaxInstances -- the ray-traced rocks_
    // pool above is deliberately kept small for exactly that budget reason.
    //
    // Scanned over a grid rather than randomly scattered like trees/rocks/
    // shrubs above. terrainGravelAmount mirrors basic.frag's noisy low-area
    // and steep-slope blend, so the geometry follows the visible material
    // rather than merely assuming every gravel patch is a steep bank.
    constexpr float kGridStep = 2.5f;
    constexpr float kMinGravelAmount = 0.55f;
    constexpr float kEdgeMargin = 3.0f;
    constexpr float kMinDistanceFromSpawn = 5.0f;
    constexpr float kJitter = kGridStep * 0.5f;
    // Not every qualifying cell spawns rocks, and jitter jitters the
    // position within the cell -- both so this reads as scattered scree
    // rather than a visibly regular grid.
    constexpr float kSpawnChance = 0.72f;

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> jitterDist(-kJitter, kJitter);
    std::uniform_real_distribution<float> yawDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> scaleDist(0.08f, 0.22f);
    std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
    std::uniform_int_distribution<int> countDist(2, 5);
    std::uniform_int_distribution<int> variantDist(0, static_cast<int>(rockMeshes_.size()) - 1);

    float half = terrain_->worldSize() * 0.5f - kEdgeMargin;
    for (float gx = -half; gx <= half; gx += kGridStep) {
        for (float gz = -half; gz <= half; gz += kGridStep) {
            if (glm::length(glm::vec2(gx, gz)) < kMinDistanceFromSpawn) continue;
            if (terrainGravelAmount(*terrain_, gx, gz) < kMinGravelAmount) continue;
            if (chanceDist(rng) > kSpawnChance) continue;

            int count = countDist(rng);
            for (int k = 0; k < count; ++k) {
                float px = gx + jitterDist(rng);
                float pz = gz + jitterDist(rng);
                if (WaterGenerator::isUnderwater(waterField, px, pz)) continue;

                RockInstance rock;
                rock.position = glm::vec3(px, terrain_->heightAt(px, pz) - 0.05f, pz);
                rock.yaw = yawDist(rng);
                rock.scale = scaleDist(rng);
                rock.meshVariant = variantDist(rng);
                smallRocks_.push_back(rock);
            }
        }
    }
}

void Application::spawnDynamicLight(glm::vec3 position, glm::vec3 color, float radius,
                                     float intensity, float lifetime) {
    DynamicLight light;
    light.position = position;
    light.color = color;
    light.radius = radius;
    light.intensity = intensity;
    light.initialLifetime = light.lifetimeRemaining = lifetime;

    if (dynamicLights_.size() < static_cast<size_t>(kMaxDynamicLights)) {
        dynamicLights_.push_back(light);
        return;
    }
    // All kMaxDynamicLights slots are taken -- replace whichever existing
    // light is closest to expiring rather than dropping the new one, so a
    // fresh muzzle flash/explosion always shows up even if a burst of
    // shots/hits briefly exceeds the cap.
    auto soonest = std::min_element(
        dynamicLights_.begin(), dynamicLights_.end(),
        [](const DynamicLight& a, const DynamicLight& b) {
            return a.lifetimeRemaining < b.lifetimeRemaining;
        });
    *soonest = light;
}

void Application::spawnSmokePuff(glm::vec3 position, glm::vec3 velocity, float initialScale,
                                  float finalScale, float lifetime, bool dust) {
    SmokePuff puff;
    puff.position = position;
    puff.velocity = velocity;
    puff.initialScale = initialScale;
    puff.finalScale = finalScale;
    puff.initialLifetime = puff.lifetimeRemaining = lifetime;
    puff.dust = dust;
    smokePuffs_.push_back(puff);
}

void Application::spawnExplosion(glm::vec3 position) {
    // Bright orange flash, unshadowed -- see DynamicLight.h. Radius/
    // lifetime roughly matched to the debris burst below so nearby geometry
    // lights up for about as long as the explosion visually reads as
    // "happening".
    spawnDynamicLight(position, glm::vec3(1.0f, 0.45f, 0.12f), /*radius=*/11.0f, /*intensity=*/18.0f,
                       /*lifetime=*/0.3f);

    constexpr int kChunkCount = 7;
    constexpr int kEmberCount = 8;

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> upBias(0.35f, 1.0f);  // mostly outward+upward, not downward

    auto randomDirection = [&]() {
        return glm::normalize(glm::vec3(unit(rng), upBias(rng), unit(rng)));
    };
    auto randomAxis = [&]() { return glm::normalize(glm::vec3(unit(rng), unit(rng), unit(rng))); };

    for (int i = 0; i < kChunkCount; ++i) {
        std::uniform_real_distribution<float> speedDist(2.0f, 5.0f);
        std::uniform_real_distribution<float> scaleDist(0.25f, 0.5f);
        std::uniform_real_distribution<float> lifeDist(0.7f, 1.1f);
        std::uniform_real_distribution<float> spinDist(-8.0f, 8.0f);

        DebrisParticle chunk;
        chunk.position = position;
        chunk.velocity = randomDirection() * speedDist(rng);
        chunk.rotationAxis = randomAxis();
        chunk.rotationSpeed = spinDist(rng);
        chunk.baseScale = scaleDist(rng);
        chunk.initialLifetime = chunk.lifetimeRemaining = lifeDist(rng);
        chunk.ember = false;
        debris_.push_back(chunk);
    }

    for (int i = 0; i < kEmberCount; ++i) {
        std::uniform_real_distribution<float> speedDist(4.0f, 8.0f);
        std::uniform_real_distribution<float> scaleDist(0.1f, 0.2f);
        std::uniform_real_distribution<float> lifeDist(0.3f, 0.55f);
        std::uniform_real_distribution<float> spinDist(-16.0f, 16.0f);

        DebrisParticle ember;
        ember.position = position;
        ember.velocity = randomDirection() * speedDist(rng);
        ember.rotationAxis = randomAxis();
        ember.rotationSpeed = spinDist(rng);
        ember.baseScale = scaleDist(rng);
        ember.initialLifetime = ember.lifetimeRemaining = lifeDist(rng);
        ember.ember = true;
        debris_.push_back(ember);
    }
}

void Application::destroyBox(Box& box) {
    box.alive = false;
    ImpactEffect effect;
    effect.position = box.position;
    impactEffects_.push_back(effect);
    spawnExplosion(box.position);
}

void Application::updateTrackMarks(float deltaTime) {
    constexpr float kTrackMarkSpacing = 0.34f;
    constexpr float kDustSpacing = 0.72f;
    constexpr size_t kMaxTrackMarks = 256;
    const glm::vec3 contactPositions[] = {
        tank_->leftTrackGroundPosition(),
        tank_->rightTrackGroundPosition(),
    };
    const float contactSpeeds[] = {
        tank_->leftTrackGroundSpeed(),
        tank_->rightTrackGroundSpeed(),
    };
    const float contactAmounts[] = {
        tank_->leftTrackContactAmount(),
        tank_->rightTrackContactAmount(),
    };

    for (size_t trackIndex = 0; trackIndex < trackTrails_.size(); ++trackIndex) {
        TrackTrailState& trail = trackTrails_[trackIndex];
        glm::vec3 current = contactPositions[trackIndex];
        current.y = terrain_->heightAt(current.x, current.z);
        if (!trail.initialized) {
            trail.previousPosition = current;
            trail.initialized = true;
            continue;
        }

        glm::vec3 segment = current - trail.previousPosition;
        float segmentLength = glm::length(segment);
        if (segmentLength < 1e-5f) continue;
        glm::vec3 travelDirection = segment / segmentLength;
        float trackSpeed = contactSpeeds[trackIndex];
        float contactAmount = contactAmounts[trackIndex];
        float speedAmount = glm::smoothstep(0.25f, 6.0f, trackSpeed);
        float accelerationAmount =
            glm::clamp(std::abs(tank_->longitudinalAcceleration()) / 8.0f, 0.0f, 1.0f);
        float slipAmount = glm::clamp(tank_->lateralSlipSpeed() / 1.25f, 0.0f, 1.0f);
        float turnAmount = glm::clamp(tank_->angularSpeed() / 1.0f, 0.0f, 1.0f);
        float markIntensity = glm::clamp(0.55f + speedAmount * 0.12f +
                                             accelerationAmount * 0.18f + slipAmount * 0.12f +
                                             turnAmount * 0.12f,
                                         0.55f, 1.0f);
        float dustIntensity =
            speedAmount * (0.18f + accelerationAmount * 0.30f + slipAmount * 0.28f +
                           turnAmount * 0.28f);

        auto emitAtSpacing = [&](float& carriedDistance, float spacing, auto&& emit) {
            float traversed = 0.0f;
            float distanceToEmission = spacing - carriedDistance;
            while (traversed + distanceToEmission <= segmentLength) {
                traversed += distanceToEmission;
                emit(traversed / segmentLength);
                carriedDistance = 0.0f;
                distanceToEmission = spacing;
            }
            carriedDistance += segmentLength - traversed;
        };

        emitAtSpacing(trail.distanceSinceMark, kTrackMarkSpacing, [&](float t) {
            if (contactAmount < 0.2f) return;
            glm::vec3 point = glm::mix(trail.previousPosition, current, t);
            glm::vec3 normal = terrain_->normalAt(point.x, point.z);
            glm::vec3 tangent = travelDirection - normal * glm::dot(travelDirection, normal);
            if (glm::length(tangent) < 1e-5f) tangent = tank_->forward();

            TrackMark mark;
            mark.position = point + normal * 0.05f;
            mark.up = normal;
            mark.forward = glm::normalize(tangent);
            mark.width = tank_->trackWidth();
            mark.length = 0.9f;
            mark.intensity = markIntensity * contactAmount;
            trackMarks_.push_back(mark);
        });

        emitAtSpacing(trail.distanceSinceDust, kDustSpacing, [&](float t) {
            if (dustIntensity * contactAmount < 0.06f) return;
            glm::vec3 point = glm::mix(trail.previousPosition, current, t);
            glm::vec3 normal = terrain_->normalAt(point.x, point.z);
            glm::vec3 tangent = travelDirection - normal * glm::dot(travelDirection, normal);
            if (glm::length(tangent) < 1e-5f) tangent = tank_->forward();
            tangent = glm::normalize(tangent);
            glm::vec3 sideways = glm::normalize(glm::cross(normal, tangent));
            float noise = terrainHash(glm::vec2(point.x, point.z) * 2.7f +
                                      glm::vec2(static_cast<float>(trackIndex) * 17.0f));
            glm::vec3 velocity = normal * glm::mix(0.35f, 0.75f, noise) - tangent * 0.25f +
                                 sideways * ((noise - 0.5f) * 0.45f);
            float intensity = glm::clamp(dustIntensity * contactAmount, 0.0f, 1.0f);
            spawnSmokePuff(point + normal * 0.12f, velocity,
                           glm::mix(0.09f, 0.17f, intensity),
                           glm::mix(0.32f, 0.68f, intensity),
                           glm::mix(0.42f, 0.68f, intensity), /*dust=*/true);
        });

        trail.previousPosition = current;
    }

    for (auto& mark : trackMarks_) mark.update(deltaTime);
    trackMarks_.erase(std::remove_if(trackMarks_.begin(), trackMarks_.end(),
                                      [](const TrackMark& m) { return !m.alive; }),
                       trackMarks_.end());
    if (trackMarks_.size() > kMaxTrackMarks) {
        trackMarks_.erase(trackMarks_.begin(),
                          trackMarks_.begin() + (trackMarks_.size() - kMaxTrackMarks));
    }
}

void Application::buildAccelerationStructures() {
    boxBLAS_ = std::make_unique<AccelerationStructure>(
        AccelerationStructure::buildBLAS(*context_, *commands_, *boxMesh_));
    shellBLAS_ = std::make_unique<AccelerationStructure>(
        AccelerationStructure::buildBLAS(*context_, *commands_, *shellMesh_));
    // Ray queries only need a stable occlusion silhouette, not the raster
    // mesh's fine chips. Build proxy BLASes from the cheapest matching LODs;
    // visible close-up geometry remains completely independent.
    for (const auto& mesh : rockProxyMeshes_) {
        rockBLAS_.push_back(std::make_unique<AccelerationStructure>(
            AccelerationStructure::buildBLAS(*context_, *commands_, *mesh)));
    }
    sedimentaryCliffBLAS_ = std::make_unique<AccelerationStructure>(
        AccelerationStructure::buildBLAS(*context_, *commands_, *sedimentaryCliffMesh_));
    for (const auto& mesh : farTreeBarkMeshes_) {
        treeBarkBLAS_.push_back(std::make_unique<AccelerationStructure>(
            AccelerationStructure::buildBLAS(*context_, *commands_, *mesh)));
    }
    for (const auto& mesh : treeLeafProxyMeshes_) {
        treeLeafBLAS_.push_back(std::make_unique<AccelerationStructure>(
            AccelerationStructure::buildBLAS(*context_, *commands_, *mesh)));
    }

    auto initialInstances = gatherRayTracingInstances();
    sceneAS_ =
        std::make_unique<SceneAccelerationStructure>(*context_, *commands_, initialInstances);

    // Each frame-in-flight slot's TLAS handle never changes after this --
    // recordRebuildTLAS rebuilds its contents in place every frame, but
    // reuses the same VkAccelerationStructureKHR object -- so the
    // descriptor only needs writing once per slot here, not every frame.
    for (size_t i = 0; i < CommandContext::kFramesInFlight; ++i) {
        pipeline_->updateTLASDescriptor(i, sceneAS_->handle(i));
    }

    std::cout << "Ray tracing scene ready: " << initialInstances.size()
              << " initial instances (capacity " << SceneAccelerationStructure::kMaxInstances << ")"
              << std::endl;
}

std::vector<AccelerationStructure::Instance> Application::gatherRayTracingInstances() const {
    std::vector<AccelerationStructure::Instance> instances;
    instances.push_back({terrain_->blasAddress(), glm::mat4(1.0f)});
    for (const auto& tree : trees_) {
        instances.push_back({treeBarkBLAS_[tree.meshVariant]->deviceAddress(), tree.worldMatrix()});
        instances.push_back({treeLeafBLAS_[tree.meshVariant]->deviceAddress(), tree.worldMatrix()});
    }
    for (const auto& rock : rocks_) {
        instances.push_back({rockBLAS_[rock.meshVariant]->deviceAddress(), rock.worldMatrix()});
    }
    for (const auto& cliff : sedimentaryCliffs_) {
        instances.push_back({sedimentaryCliffBLAS_->deviceAddress(), cliff.worldMatrix()});
    }
    for (const auto& part : tank_->drawParts()) {
        instances.push_back({part.blasAddress, part.worldMatrix});
    }
    for (const auto& box : boxes_) {
        if (!box.alive) continue;
        instances.push_back({boxBLAS_->deviceAddress(), box.worldMatrix()});
    }
    for (const auto& shell : projectiles_) {
        instances.push_back({shellBLAS_->deviceAddress(), shell.worldMatrix()});
    }
    // Impact-flash effects, explosion debris, and track marks are
    // deliberately excluded -- short-lived-to-moderately-lived and numerous
    // enough (debris spawns 15 particles per explosion; a mark drops every
    // ~1.1 units of tank travel) that the added TLAS churn isn't worth it
    // just so they can cast their own (in the marks' case, essentially flat
    // and invisible anyway) shadows. Water is excluded too -- it's static
    // like terrain, but flat/thin enough that it wouldn't meaningfully
    // occlude anything; things near the shore still cast correct shadows
    // *onto* the water surface regardless, since that only depends on the
    // shadow ray's origin (the water fragment itself) and the TLAS already
    // containing the occluder, not on water being in the TLAS itself.
    return instances;
}

std::string Application::nextScreenshotPath() {
    ++screenshotCounter_;
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "%04d", screenshotCounter_);
    return std::string(ASSET_ROOT) + "/screenshots/screenshot_" + suffix + ".png";
}

void Application::recreateSwapchainDependentResources() {
    swapchain_->recreate();
    historyBuffer_->recreate(*commands_, swapchain_->extent());
    for (size_t i = 0; i < CommandContext::kFramesInFlight; ++i) {
        size_t readSlot = 1 - i;
        pipeline_->updateHistoryDescriptor(i, historyBuffer_->imageView(readSlot),
                                            historyBuffer_->sampler());
    }
}

void Application::fireProjectile() {
    constexpr float kShellSpeed = 25.0f;

    Projectile shell;
    shell.position = tank_->muzzleWorldPosition();
    shell.previousPosition = shell.position;
    shell.velocity = tank_->aimDirection() * kShellSpeed;
    projectiles_.push_back(shell);

    // Warm, small, very short-lived flash at the muzzle -- see
    // DynamicLight.h. Much punchier/briefer than the explosion light below
    // (0.08s vs 0.3s) so it reads as an instantaneous flash, not a glow.
    spawnDynamicLight(shell.position, glm::vec3(1.0f, 0.85f, 0.5f), /*radius=*/5.0f, /*intensity=*/25.0f,
                       /*lifetime=*/0.08f);

    // Muzzle blast: a small burst of smoke puffs kicked mostly forward
    // along the barrel (like a real muzzle blast) with some outward spread
    // and a little upward drift, not a single puff -- one puff alone reads
    // as a ball, a handful with varied speed/spread/scale reads as an
    // actual blast of gas dissipating.
    std::mt19937 blastRng(std::random_device{}());
    std::uniform_real_distribution<float> blastSpread(-0.5f, 0.5f);
    std::uniform_real_distribution<float> blastSpeed(1.5f, 4.0f);
    std::uniform_real_distribution<float> blastScale(0.3f, 0.5f);
    glm::vec3 blastUp(0.0f, 1.0f, 0.0f);
    glm::vec3 blastRight = glm::normalize(glm::cross(blastUp, shell.velocity));
    for (int i = 0; i < 5; ++i) {
        glm::vec3 spread = blastRight * blastSpread(blastRng) + blastUp * blastSpread(blastRng);
        glm::vec3 puffVelocity =
            glm::normalize(tank_->aimDirection() + spread * 0.6f) * blastSpeed(blastRng) +
            blastUp * 0.6f;
        spawnSmokePuff(shell.position, puffVelocity, blastScale(blastRng), blastScale(blastRng) * 2.2f,
                       /*lifetime=*/0.5f);
    }
}

void Application::updateProjectilesAndCollisions(float deltaTime) {
    // Every kTrailSpacing units of travel, drop a small fading wisp behind
    // the shell -- distance-based rather than one per frame (same idea as
    // updateTrackMarks' spacing for the tank's own tread marks), so the
    // trail's density on screen doesn't depend on framerate and doesn't
    // flood smokePuffs_ with a puff every single frame at 25 units/sec.
    constexpr float kTrailSpacing = 1.1f;
    for (auto& shell : projectiles_) {
        if (!shell.alive) continue;
        glm::vec3 prePosition = shell.position;
        shell.update(deltaTime);
        shell.distanceSinceLastPuff += glm::length(shell.position - prePosition);
        if (shell.distanceSinceLastPuff >= kTrailSpacing) {
            shell.distanceSinceLastPuff -= kTrailSpacing;
            // Slow, mostly-upward drift and a short lifetime/small scale --
            // a wisp, not another blast -- with a little backward velocity
            // (relative to the shell) so it doesn't look like it's still
            // glued to and moving with the shell that dropped it.
            glm::vec3 puffVelocity = -glm::normalize(shell.velocity) * 0.4f + glm::vec3(0.0f, 0.5f, 0.0f);
            spawnSmokePuff(shell.position, puffVelocity, 0.18f, 0.4f, /*lifetime=*/0.35f);
        }
        // Shared by every hit case below: drop the shell, spawn the flash +
        // explosion at the actual entry point along the segment (not just
        // the shell's post-move position, which can already be well past
        // the surface it hit for a fast-moving shell).
        auto triggerHit = [&](glm::vec3 hitPoint) {
            shell.alive = false;
            ImpactEffect effect;
            effect.position = hitPoint;
            impactEffects_.push_back(effect);
            spawnExplosion(hitPoint);
        };

        for (auto& box : boxes_) {
            if (!box.alive) continue;
            float t = 0.0f;
            if (CollisionSystem::segmentIntersectsAABB(shell.previousPosition, shell.position,
                                                         box.aabbMin(), box.aabbMax(), &t)) {
                box.alive = false;
                triggerHit(glm::mix(shell.previousPosition, shell.position, t));
                break;
            }
        }

        // Trees/rocks are static and never destroyed (unlike boxes), so
        // there's no obstacles_-style shared list to reuse here -- that one
        // only carries an XZ circle sized for the tank's ground-level trunk
        // collision, which would let a shell fly straight through a tree's
        // actual (much wider/taller) canopy untouched. These spheres are
        // sized/centered to roughly match what's actually drawn instead.
        if (shell.alive) {
            for (const auto& tree : trees_) {
                glm::vec3 canopyCenter = tree.position + glm::vec3(0.0f, 1.0f * tree.scale, 0.0f);
                float t = 0.0f;
                if (CollisionSystem::segmentIntersectsSphere(shell.previousPosition, shell.position,
                                                               canopyCenter, 1.1f * tree.scale, &t)) {
                    triggerHit(glm::mix(shell.previousPosition, shell.position, t));
                    break;
                }
            }
        }
        if (shell.alive) {
            for (const auto& rock : rocks_) {
                glm::vec3 rockCenter = rock.position + glm::vec3(0.0f, 0.4f * rock.scale, 0.0f);
                float t = 0.0f;
                if (CollisionSystem::segmentIntersectsSphere(shell.previousPosition, shell.position,
                                                               rockCenter, 0.85f * rock.scale, &t)) {
                    triggerHit(glm::mix(shell.previousPosition, shell.position, t));
                    break;
                }
            }
        }
        // Terrain last -- a catch-all "the shell has embedded itself in the
        // ground" check, deliberately checked after every specific object
        // above so a shell that clips a tree/rock right at ground level
        // reads as hitting that object, not the ground under it.
        if (shell.alive) {
            float groundHeight = terrain_->heightAt(shell.position.x, shell.position.z);
            if (shell.position.y <= groundHeight) {
                triggerHit(glm::vec3(shell.position.x, groundHeight, shell.position.z));
            }
        }
    }

    projectiles_.erase(
        std::remove_if(projectiles_.begin(), projectiles_.end(),
                        [](const Projectile& p) { return !p.alive; }),
        projectiles_.end());

    // The tank drives through crates rather than being blocked by them
    // (unlike trees/rocks, boxes are never added to obstacles_) -- instead,
    // getting close enough destroys them, same explosion as a shell hit.
    // Circle (tank, XZ only)-vs-AABB overlap: clamp the tank's position to
    // the box's AABB to find the nearest point on it, then check the
    // distance to that point against the tank's own collision radius
    // (matches the radius Tank::update uses for tree/rock collision).
    float tankCollisionRadius = tank_->hullWidth() * 0.6f;
    glm::vec3 tankPos = tank_->position();
    for (auto& box : boxes_) {
        if (!box.alive) continue;
        glm::vec3 aabbMin = box.aabbMin();
        glm::vec3 aabbMax = box.aabbMax();
        float closestX = glm::clamp(tankPos.x, aabbMin.x, aabbMax.x);
        float closestZ = glm::clamp(tankPos.z, aabbMin.z, aabbMax.z);
        float dx = tankPos.x - closestX;
        float dz = tankPos.z - closestZ;
        if (dx * dx + dz * dz < tankCollisionRadius * tankCollisionRadius) {
            destroyBox(box);
        }
    }

    for (auto& effect : impactEffects_) effect.update(deltaTime);
    impactEffects_.erase(
        std::remove_if(impactEffects_.begin(), impactEffects_.end(),
                        [](const ImpactEffect& e) { return !e.alive; }),
        impactEffects_.end());

    for (auto& particle : debris_) particle.update(deltaTime);
    debris_.erase(std::remove_if(debris_.begin(), debris_.end(),
                                  [](const DebrisParticle& d) { return !d.alive; }),
                  debris_.end());

    for (auto& light : dynamicLights_) light.update(deltaTime);
    dynamicLights_.erase(
        std::remove_if(dynamicLights_.begin(), dynamicLights_.end(),
                        [](const DynamicLight& l) { return !l.alive; }),
        dynamicLights_.end());

    for (auto& puff : smokePuffs_) puff.update(deltaTime);
    smokePuffs_.erase(std::remove_if(smokePuffs_.begin(), smokePuffs_.end(),
                                      [](const SmokePuff& p) { return !p.alive; }),
                       smokePuffs_.end());
}

void Application::drawFrame() {
    auto& frame = commands_->frame(currentFrame_);

    VK_CHECK(vkWaitForFences(context_->device(), 1, &frame.inFlight, VK_TRUE, UINT64_MAX));

    // Also wait on the OTHER frame-in-flight slot's fence before touching
    // the history buffer. This slot's write target (historyBuffer_->image
    // (currentFrame_)) was the OTHER slot's *read* input last frame (see
    // updateHistoryDescriptor's readSlot = 1-i pairing); waiting only on
    // this slot's own fence guarantees this slot's prior *write* finished,
    // but says nothing about whether the other slot's GPU work (which reads
    // this same image) has completed yet. Without this, that read can race
    // with this frame's write to the same image -- a real write-after-read
    // hazard that showed up as unstable, noisy shadow accumulation even
    // while the camera/tank were both stationary. Two frames-in-flight
    // means this costs little (it's rarely still pending by the time we get
    // here) while making the history ping-pong actually safe.
    auto& otherFrame = commands_->frame(1 - currentFrame_);
    VK_CHECK(vkWaitForFences(context_->device(), 1, &otherFrame.inFlight, VK_TRUE, UINT64_MAX));

    // This slot's fence guarantees its previous timestamps are complete.
    // Read them before resetting/reusing the same query range below; no GPU
    // wait or pipeline bubble is introduced by the profiler.
    uint32_t timestampBase = static_cast<uint32_t>(currentFrame_) * kGpuTimestampsPerFrame;
    if (gpuTimestampsReady_[currentFrame_]) {
        std::array<uint64_t, kGpuTimestampsPerFrame> timestamps{};
        VkResult timestampResult = vkGetQueryPoolResults(
            context_->device(), gpuTimestampPool_, timestampBase, kGpuTimestampsPerFrame,
            sizeof(timestamps), timestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
        if (timestampResult == VK_SUCCESS) {
            auto elapsedMs = [&](uint32_t begin, uint32_t end) {
                return static_cast<float>(timestamps[end] - timestamps[begin]) *
                       gpuTimestampPeriodNs_ / 1'000'000.0f;
            };
            float tlasMs = elapsedMs(0, 1);
            float terrainMs = elapsedMs(1, 2);
            float foregroundMs = elapsedMs(2, 3);
            float sceneryMs = elapsedMs(3, 4);
            float effectsMs = elapsedMs(4, 6);
            float hudMs = elapsedMs(6, 7);
            float totalMs = elapsedMs(0, 7);
            float blend = gpuTimingInitialized_ ? 0.1f : 1.0f;
            gpuTlasMs_ = glm::mix(gpuTlasMs_, tlasMs, blend);
            gpuTerrainMs_ = glm::mix(gpuTerrainMs_, terrainMs, blend);
            gpuForegroundMs_ = glm::mix(gpuForegroundMs_, foregroundMs, blend);
            gpuSceneryMs_ = glm::mix(gpuSceneryMs_, sceneryMs, blend);
            gpuEffectsMs_ = glm::mix(gpuEffectsMs_, effectsMs, blend);
            gpuHudMs_ = glm::mix(gpuHudMs_, hudMs, blend);
            gpuTotalMs_ = glm::mix(gpuTotalMs_, totalMs, blend);
            gpuTimingInitialized_ = true;

            if (frameCounter_ > 0 && frameCounter_ % 120 == 0) {
                std::cout << std::fixed << std::setprecision(2)
                          << "GPU: " << gpuTotalMs_ << " ms total (TLAS " << gpuTlasMs_
                          << ", terrain " << gpuTerrainMs_ << ", foreground " << gpuForegroundMs_
                          << ", scenery " << gpuSceneryMs_ << ", effects " << gpuEffectsMs_
                          << ", HUD " << gpuHudMs_ << " ms)"
                          << std::defaultfloat << std::endl;
            }
        }
    }

    uint32_t imageIndex = 0;
    VkResult acquireResult =
        vkAcquireNextImageKHR(context_->device(), swapchain_->handle(), UINT64_MAX,
                               frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchainDependentResources();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swapchain image");
    }

    VK_CHECK(vkResetFences(context_->device(), 1, &frame.inFlight));
    VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));

    VkExtent2D extent = swapchain_->extent();
    float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    Pipeline::FrameUBO ubo{};
    ubo.view = camera_.viewMatrix();
    ubo.proj = camera_.projMatrix(aspect);
    // On the very first frame there's no real "previous" matrix yet --
    // using the leftover identity default would make reprojection treat
    // world position as if it were already clip space, which can land
    // inside the [0,1] UV bounds check near the world origin and read
    // scrambled, unrelated history-texture pixels there. Self-reproject
    // (this frame onto itself) instead, which is always valid and just
    // reads the neutral 1.0 the history buffer was cleared to.
    if (firstFrame_) {
        prevViewProj_ = ubo.proj * ubo.view;
        prevCameraPos_ = camera_.position();
        firstFrame_ = false;
    }
    ubo.prevViewProj = prevViewProj_;
    ubo.prevCameraPos = glm::vec4(prevCameraPos_, 0.0f);
    ubo.lightDir = glm::vec4(glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)), 0.0f);
    // cameraPos.w rides along as a per-frame varying seed for the shadow
    // jitter in basic.frag -- without it, the jitter is a pure function of
    // gl_FragCoord alone, so a static camera+tank gets the IDENTICAL 3
    // sample directions every frame. Temporal accumulation then has nothing
    // to actually average over time: it just converges immediately to
    // whichever single noisy 3-sample dice-roll each pixel happened to get,
    // which reads as a fixed, blotchy, overly dark pattern rather than a
    // smooth soft shadow. Varying the seed each frame is what lets many
    // frames' worth of *different* samples actually accumulate into
    // something smooth.
    ubo.cameraPos = glm::vec4(camera_.position(), static_cast<float>(frameCounter_ % 1024));
    // dynamicLights_ is already capped at kMaxDynamicLights (see
    // spawnDynamicLight), so this is a direct copy; the UBO arrays default-
    // initialize to all-zero (see Pipeline::FrameUBO), which basic.frag
    // reads as "inactive slot" via the w=radius/w=intensity being 0.
    for (size_t i = 0; i < dynamicLights_.size(); ++i) {
        const DynamicLight& light = dynamicLights_[i];
        ubo.dynamicLightPosRadius[i] = glm::vec4(light.position, light.radius);
        ubo.dynamicLightColorIntensity[i] = glm::vec4(light.color, light.currentIntensity());
    }
    pipeline_->updateFrameUBO(ubo);

    // Cull repeated static props and group their transforms by shared
    // mesh/material variant and projected-size LOD. Each non-empty group is
    // still one instanced draw; adding LODs does not regress to per-object
    // draw calls. Hysteresis stored on each instance prevents threshold
    // flicker while the camera or tank moves slowly.
    glm::mat4 viewProjection = ubo.proj * ubo.view;
    constexpr size_t kLodCount = 3;
    const size_t treeVariantCount = treeBarkMeshes_.size();
    const size_t rockVariantCount = rockMeshes_.size();
    std::vector<std::vector<glm::mat4>> treeGroups(treeVariantCount * kLodCount);
    std::vector<std::vector<glm::mat4>> rockGroups(rockVariantCount * kLodCount);
    std::vector<std::vector<glm::mat4>> smallRockGroups(smallRockMeshes_.size());
    std::vector<std::vector<glm::mat4>> shrubGroups(shrubMeshes_.size());
    std::vector<std::vector<glm::mat4>> cliffGroups(1);
    float viewportHeight = static_cast<float>(swapchain_->extent().height);

    for (TreeInstance& tree : trees_) {
        glm::vec3 center = tree.position + glm::vec3(0.0f, 2.4f * tree.scale, 0.0f);
        float radius = 3.5f * tree.scale;
        if (!sphereIntersectsFrustum(viewProjection, center, radius)) continue;
        float projectedRadius =
            projectedRadiusPixels(ubo.view, ubo.proj, viewportHeight, center, radius);
        tree.lod = selectLodWithHysteresis(tree.lod, projectedRadius,
                                           /*nearThreshold=*/60.0f,
                                           /*farThreshold=*/32.0f);
        size_t group = static_cast<size_t>(tree.lod) * treeVariantCount + tree.meshVariant;
        treeGroups[group].push_back(tree.worldMatrix());
    }
    for (RockInstance& rock : rocks_) {
        glm::vec3 center = rock.position + glm::vec3(0.0f, 0.4f * rock.scale, 0.0f);
        float radius = 1.3f * rock.scale;
        if (!sphereIntersectsFrustum(viewProjection, center, radius)) continue;
        float projectedRadius =
            projectedRadiusPixels(ubo.view, ubo.proj, viewportHeight, center, radius);
        rock.lod = selectLodWithHysteresis(rock.lod, projectedRadius,
                                           /*nearThreshold=*/55.0f,
                                           /*farThreshold=*/20.0f);
        size_t group = static_cast<size_t>(rock.lod) * rockVariantCount + rock.meshVariant;
        rockGroups[group].push_back(rock.worldMatrix());
    }
    for (const RockInstance& rock : smallRocks_) {
        constexpr float kSmallRockDrawDistance = 55.0f;
        if (glm::distance(camera_.position(), rock.position) > kSmallRockDrawDistance) continue;
        glm::vec3 center = rock.position + glm::vec3(0.0f, 0.15f * rock.scale, 0.0f);
        if (sphereIntersectsFrustum(viewProjection, center, 0.4f * rock.scale))
            smallRockGroups[rock.meshVariant].push_back(rock.worldMatrix());
    }
    for (const ShrubInstance& shrub : shrubs_) {
        glm::vec3 center = shrub.position + glm::vec3(0.0f, 0.3f * shrub.scale, 0.0f);
        if (sphereIntersectsFrustum(viewProjection, center, 0.8f * shrub.scale))
            shrubGroups[shrub.meshVariant].push_back(shrub.worldMatrix());
    }
    for (const RockInstance& cliff : sedimentaryCliffs_) {
        glm::vec3 center = cliff.position + glm::vec3(0.0f, 0.25f * cliff.scale, 0.0f);
        if (sphereIntersectsFrustum(viewProjection, center, 5.0f * cliff.scale))
            cliffGroups[0].push_back(cliff.worldMatrix());
    }

    struct InstanceBatch {
        uint32_t first = 0;
        uint32_t count = 0;
    };
    std::vector<glm::mat4> rasterInstanceTransforms;
    rasterInstanceTransforms.reserve(trees_.size() + rocks_.size() + smallRocks_.size() +
                                     shrubs_.size() + sedimentaryCliffs_.size());
    auto appendGroups = [&](const std::vector<std::vector<glm::mat4>>& groups) {
        std::vector<InstanceBatch> batches(groups.size());
        for (size_t variant = 0; variant < groups.size(); ++variant) {
            batches[variant].first = static_cast<uint32_t>(rasterInstanceTransforms.size());
            batches[variant].count = static_cast<uint32_t>(groups[variant].size());
            rasterInstanceTransforms.insert(rasterInstanceTransforms.end(), groups[variant].begin(),
                                            groups[variant].end());
        }
        return batches;
    };
    std::vector<InstanceBatch> treeBatches = appendGroups(treeGroups);
    std::vector<InstanceBatch> rockBatches = appendGroups(rockGroups);
    std::vector<InstanceBatch> smallRockBatches = appendGroups(smallRockGroups);
    std::vector<InstanceBatch> shrubBatches = appendGroups(shrubGroups);
    std::vector<InstanceBatch> cliffBatches = appendGroups(cliffGroups);
    pipeline_->updateInstanceTransforms(rasterInstanceTransforms);
    prevViewProj_ = ubo.proj * ubo.view;
    prevCameraPos_ = camera_.position();
    ++frameCounter_;

    // See ScreenshotRequest's comment: read straight from GPU memory later
    // in this same command buffer (right before the present transition,
    // once everything -- including the HUD -- has been drawn) rather than
    // relying on any OS-level screenshot tool.
    bool captureScreenshot = screenshotRequest_.has_value() && frameCounter_ == screenshotRequest_->atFrame;
    std::unique_ptr<Buffer> screenshotBuffer;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    vkCmdResetQueryPool(frame.commandBuffer, gpuTimestampPool_, timestampBase,
                        kGpuTimestampsPerFrame);
    vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         gpuTimestampPool_, timestampBase + 0);

    // Rebuild this frame-in-flight slot's TLAS from the current scene state
    // (acceleration structure builds can't happen inside a dynamic
    // rendering scope, so this must be before vkCmdBeginRendering). Read by
    // basic.frag via ray query for shadow tracing.
    sceneAS_->rebuild(frame.commandBuffer, currentFrame_, gatherRayTracingInstances());
    vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         gpuTimestampPool_, timestampBase + 1);

    VkImage colorImage = swapchain_->image(imageIndex);
    VkImage historyWriteImage = historyBuffer_->image(currentFrame_);
    // The presentable swapchain image and the persistent history slot above
    // are both single-sample and now serve as MSAA *resolve targets* rather
    // than the attachments actually drawn into -- the pipeline (created with
    // rasterizationSamples = ctx_.msaaSamples()) renders into these
    // multisampled scratch images instead, which the driver resolves
    // (averages down) into the single-sample targets at the end of the
    // render pass (see the resolveImageView fields below).
    VkImage msaaColorImage = swapchain_->colorImage();
    VkImage msaaHistoryImage = historyBuffer_->msaaImage();

    // All attachments are fully overwritten this frame (LOAD_OP_CLEAR), so
    // treating oldLayout as UNDEFINED is correct regardless of prior layout:
    // it tells the driver not to preserve contents, matching the clear. The
    // resolve targets (colorImage, historyWriteImage) also need to be in
    // COLOR_ATTACHMENT_OPTIMAL up front since that's the layout the resolve
    // operation writes through.
    VkImageMemoryBarrier2 toAttachments[] = {
        imageBarrier(msaaColorImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
        imageBarrier(colorImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
        imageBarrier(swapchain_->depthImage(), VK_IMAGE_ASPECT_DEPTH_BIT,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
        imageBarrier(msaaHistoryImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
        // This slot's previous content (from 2 frames ago) was already
        // transitioned to SHADER_READ_ONLY_OPTIMAL for the OTHER slot's use
        // as history input last frame; oldLayout=UNDEFINED just discards it,
        // which is fine since we're about to overwrite it (via resolve) here.
        imageBarrier(historyWriteImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
    };

    VkMemoryBarrier2 asToShaderBarrier{};
    asToShaderBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    asToShaderBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    asToShaderBarrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    asToShaderBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    asToShaderBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.memoryBarrierCount = 1;
    depInfo.pMemoryBarriers = &asToShaderBarrier;
    depInfo.imageMemoryBarrierCount = 5;
    depInfo.pImageMemoryBarriers = toAttachments;
    vkCmdPipelineBarrier2(frame.commandBuffer, &depInfo);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchain_->colorImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    colorAttachment.resolveImageView = swapchain_->imageView(imageIndex);
    colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.clearValue.color = {{0.45f, 0.65f, 0.85f, 1.0f}};

    VkRenderingAttachmentInfo historyAttachment{};
    historyAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    historyAttachment.imageView = historyBuffer_->msaaImageView();
    historyAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    historyAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    historyAttachment.resolveImageView = historyBuffer_->imageView(currentFrame_);
    historyAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    historyAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    historyAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    historyAttachment.clearValue.color.float32[0] = 1.0f;      // neutral: "fully lit" where nothing draws
    historyAttachment.clearValue.color.float32[1] = 1.0f;      // neutral: "no AO occlusion"
    historyAttachment.clearValue.color.float32[2] = 50000.0f;  // huge distance: always fails disocclusion check

    VkRenderingAttachmentInfo colorAttachments[] = {colorAttachment, historyAttachment};

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = swapchain_->depthImageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 2;
    renderingInfo.pColorAttachments = colorAttachments;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);

    // Negative viewport height corrects Vulkan's flipped-Y NDC while leaving
    // winding/culling exactly as GLM's right-handed conventions expect (see
    // Pipeline's VK_FRONT_FACE_COUNTER_CLOCKWISE).
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(extent.height);
    viewport.width = static_cast<float>(extent.width);
    viewport.height = -static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());
    VkDescriptorSet descriptorSet = pipeline_->descriptorSet();
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 0, 1, &descriptorSet, 0, nullptr);

    VkDescriptorSet tlasSet = pipeline_->tlasDescriptorSet(currentFrame_);
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 2, 1, &tlasSet, 0, nullptr);

    VkDescriptorSet historySet = pipeline_->historyDescriptorSet(currentFrame_);
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 3, 1, &historySet, 0, nullptr);

    // Sky dome: drawn first and centered on the camera every frame (a fixed
    // radius comfortably inside the camera's far plane of 200, well beyond
    // anything else in the scene), so it always surrounds the viewer.
    // Unlit -- clouds don't need real shading -- and deliberately never
    // added to gatherRayTracingInstances: it's a fake backdrop that moves
    // with the camera, not real scene geometry, and including it would
    // make every shadow/AO/reflection ray falsely register it as an
    // occluder in every direction beyond its radius.
    constexpr float kSkyDomeRadius = 150.0f;
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &cloudMaterialSet_, 0, nullptr);
    Pipeline::PushConstants skyPc{};
    skyPc.model = glm::scale(glm::translate(glm::mat4(1.0f), camera_.position()),
                              glm::vec3(kSkyDomeRadius));
    skyPc.unlit = 1.0f;
    vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(skyPc),
                        &skyPc);
    cloudDomeMesh_->bindAndDraw(frame.commandBuffer);

    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &terrainMaterialSet_, 0, nullptr);
    Pipeline::PushConstants terrainPc{};
    terrainPc.model = glm::mat4(1.0f);
    terrainPc.heightBlend = 1.0f;
    terrainPc.materialType = 1.0f;
    vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                        sizeof(terrainPc), &terrainPc);
    terrain_->bindAndDraw(frame.commandBuffer);
    vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         gpuTimestampPool_, timestampBase + 2);

    // Play-area boundary line: a red decal ring painted onto the ground,
    // drawn right after terrain like track marks below. Lit (not unlit) --
    // "slightly lit" per the design brief -- so it still reads as sitting
    // on the grass under the scene's real lighting/shadow rather than
    // glowing flat regardless of time of day; its own texture alpha (see
    // BoundaryTextureGenerator::generateGroundLine) supplies the soft,
    // noise-roughened edge, so opacity stays at the default 1.0.
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &boundaryLineMaterialSet_, 0, nullptr);
    Pipeline::PushConstants boundaryLinePc{};
    boundaryLinePc.model = glm::mat4(1.0f);  // already built in world space, see BoundaryGenerator
    vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                        sizeof(boundaryLinePc), &boundaryLinePc);
    boundaryLineMesh_->bindAndDraw(frame.commandBuffer);

    // Track marks: flat, fading ground decals drawn right after terrain so
    // they composite on top of it. Lit (not unlit) so they still receive
    // the scene's real ray-traced shadow/AO like any other ground surface,
    // rather than writing a placeholder into the history buffer that would
    // otherwise interrupt terrain's temporal accumulation wherever they sit.
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &trackMaterialSet_, 0, nullptr);
    for (const auto& mark : trackMarks_) {
        Pipeline::PushConstants markPc{};
        markPc.model = mark.worldMatrix();
        markPc.opacity = mark.opacity();
        markPc.bumpStrength = 12.0f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(markPc), &markPc);
        trackMarkMesh_->bindAndDraw(frame.commandBuffer);
    }

    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &whiteMaterialSet_, 0, nullptr);

    // Water: drawn after track marks so a body of water correctly covers
    // any mark left at/under its surface. specularStrength gives it the
    // same shiny Fresnel highlight the tank's dull metal uses; reflectivity
    // is set much higher than the tank's (a real lake's surface reflects
    // its surroundings far more strongly than brushed metal does) so the
    // RT-reflection term reads as an actual reflective water surface rather
    // than the tank's subtle sheen. Vertex color (baked per-vertex by depth
    // in WaterGenerator) does the shallow-to-deep tinting, texture is just
    // the shared plain white.
    if (waterMesh_) {
        Pipeline::PushConstants waterPc{};
        waterPc.model = glm::mat4(1.0f);
        waterPc.specularStrength = 0.5f;
        waterPc.reflectivity = 0.4f;
        waterPc.opacity = 0.65f;
        waterPc.waveStrength = 0.02f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(waterPc), &waterPc);
        waterMesh_->bindAndDraw(frame.commandBuffer);
    }

    // Tank: painted parts (hull, turret) get the camo texture; bare-metal
    // parts (tracks, barrel) get a plain gunmetal texture instead -- see
    // CamoTextureGenerator/MetalTextureGenerator and Tank::DrawPart::
    // metallic. Tank::load's vertex color is kept near-white so either
    // texture's own baked colors show through unmodified, the same
    // reasoning as the crate/track/bark/leaf textures. No reflectivity on
    // either -- the tank's own per-pixel specular map (see basic.frag's
    // isDynamicObject branch) carries the metal/paint highlight instead, so
    // a real traced reflection on top just muddied it without adding much.
    for (const auto& part : tank_->drawParts()) {
        VkDescriptorSet materialSet = part.metallic ? metalMaterialSet_ : camoMaterialSet_;
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
                                 1, 1, &materialSet, 0, nullptr);
        Pipeline::PushConstants tankPc{};
        tankPc.model = part.worldMatrix;
        tankPc.specularStrength = part.metallic ? 0.35f : 0.12f;
        tankPc.reflectivity = 0.0f;
        // See Pipeline::PushConstants::isDynamicObject -- specularStrength
        // alone no longer uniquely identifies the tank now that its own
        // parts use different values.
        tankPc.isDynamicObject = 1.0f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(tankPc), &tankPc);
        part.mesh->bindAndDraw(frame.commandBuffer);
    }

    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &crateMaterialSet_, 0, nullptr);
    for (const auto& box : boxes_) {
        if (!box.alive) continue;
        Pipeline::PushConstants boxPc{};
        boxPc.model = box.worldMatrix();
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(boxPc), &boxPc);
        boxMesh_->bindAndDraw(frame.commandBuffer);
    }
    vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         gpuTimestampPool_, timestampBase + 3);

    // Trees: bark and leaves are separate meshes/materials (see
    // Mesh::treeBark/treeLeaves), and each LOD/variant group remains one
    // instanced draw. Bark and foliage consume the same transform batch so
    // their independently generated matching meshes stay aligned.
    for (size_t group = 0; group < treeBatches.size(); ++group) {
        const InstanceBatch& batch = treeBatches[group];
        if (batch.count == 0) continue;
        size_t lod = group / treeVariantCount;
        size_t variant = group % treeVariantCount;
        const Mesh* barkMesh = lod == 0   ? treeBarkMeshes_[variant].get()
                               : lod == 1 ? mediumTreeBarkMeshes_[variant].get()
                                          : farTreeBarkMeshes_[variant].get();
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
                                 1, 1, &barkMaterialSets_[variant], 0, nullptr);
        Pipeline::PushConstants treePc{};
        treePc.isInstanced = 1.0f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(treePc), &treePc);
        barkMesh->bindAndDrawInstanced(frame.commandBuffer, batch.count, batch.first);
    }

    for (size_t group = 0; group < treeBatches.size(); ++group) {
        const InstanceBatch& batch = treeBatches[group];
        if (batch.count == 0) continue;
        size_t lod = group / treeVariantCount;
        size_t variant = group % treeVariantCount;
        const Mesh* leafMesh = lod == 0   ? treeLeafMeshes_[variant].get()
                               : lod == 1 ? mediumTreeLeafMeshes_[variant].get()
                                          : farTreeLeafMeshes_[variant].get();
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
                                 1, 1, &leafMaterialSets_[variant], 0, nullptr);
        Pipeline::PushConstants leafPc{};
        leafPc.materialType = 2.0f;
        leafPc.isInstanced = 1.0f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(leafPc), &leafPc);
        leafMesh->bindAndDrawInstanced(frame.commandBuffer, batch.count, batch.first);
    }

    // Rocks: one culled, instanced draw per geometry/material/LOD group.
    for (size_t group = 0; group < rockBatches.size(); ++group) {
        const InstanceBatch& batch = rockBatches[group];
        if (batch.count == 0) continue;
        size_t lod = group / rockVariantCount;
        size_t variant = group % rockVariantCount;
        const Mesh* rockMesh = lod == 0   ? rockMeshes_[variant].get()
                               : lod == 1 ? mediumRockMeshes_[variant].get()
                                          : smallRockMeshes_[variant].get();
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
                                 1, 1, &rockMaterialSets_[variant], 0, nullptr);
        Pipeline::PushConstants rockPc{};
        rockPc.materialType = 3.0f;
        rockPc.isInstanced = 1.0f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(rockPc), &rockPc);
        rockMesh->bindAndDrawInstanced(frame.commandBuffer, batch.count, batch.first);
    }

    // Small scree/pebbles use their dedicated low-detail geometry and one
    // culled, instanced draw per material variant. They are not ray-traced.
    for (size_t variant = 0; variant < smallRockBatches.size(); ++variant) {
        const InstanceBatch& batch = smallRockBatches[variant];
        if (batch.count == 0) continue;
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
                                 1, 1, &rockMaterialSets_[variant], 0, nullptr);
        Pipeline::PushConstants rockPc{};
        rockPc.materialType = 3.0f;
        rockPc.isInstanced = 1.0f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(rockPc), &rockPc);
        smallRockMeshes_[variant]->bindAndDrawInstanced(frame.commandBuffer, batch.count, batch.first);
    }

    // Layered cliff outcrops use a warm gravel variant, but distinct
    // geometry and placement from the ordinary boulder pool.
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &rockMaterialSets_[1], 0, nullptr);
    const InstanceBatch& cliffBatch = cliffBatches[0];
    if (cliffBatch.count > 0) {
        Pipeline::PushConstants cliffPc{};
        cliffPc.materialType = 3.0f;
        cliffPc.isInstanced = 1.0f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(cliffPc), &cliffPc);
        sedimentaryCliffMesh_->bindAndDrawInstanced(frame.commandBuffer, cliffBatch.count,
                                                     cliffBatch.first);
    }

    // A matching cap covers the stone tops and extends down around their
    // edges as a substantial turf layer, while the deeper fractured faces
    // remain exposed rock.
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &terrainMaterialSet_, 0, nullptr);
    if (cliffBatch.count > 0) {
        Pipeline::PushConstants grassPc{};
        grassPc.materialType = 1.0f;
        grassPc.heightBlend = 1.0f;
        grassPc.isInstanced = 1.0f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(grassPc), &grassPc);
        sedimentaryCliffGrassMesh_->bindAndDrawInstanced(frame.commandBuffer, cliffBatch.count,
                                                          cliffBatch.first);
    }

    // Shrubs -- reuse leafMaterialSets_ (see their mesh creation comment).
    for (size_t variant = 0; variant < shrubBatches.size(); ++variant) {
        const InstanceBatch& batch = shrubBatches[variant];
        if (batch.count == 0) continue;
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
                                 1, 1, &leafMaterialSets_[variant], 0, nullptr);
        Pipeline::PushConstants shrubPc{};
        shrubPc.materialType = 2.0f;
        shrubPc.isInstanced = 1.0f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(shrubPc), &shrubPc);
        shrubMeshes_[variant]->bindAndDrawInstanced(frame.commandBuffer, batch.count, batch.first);
    }
    vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         gpuTimestampPool_, timestampBase + 4);

    // Play-area boundary wall: a translucent "wall of light" rising from
    // the boundary line, drawn late so it composites on top of the terrain/
    // trees/tank behind it. Unlit -- it should read as glowing energy, not
    // a lit surface -- with its own texture alpha (see
    // BoundaryTextureGenerator::generateWall) supplying both the "mostly
    // transparent" base level and the fade-to-invisible-with-height curve.
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &boundaryWallMaterialSet_, 0, nullptr);
    Pipeline::PushConstants boundaryWallPc{};
    boundaryWallPc.model = glm::mat4(1.0f);  // already built in world space, see BoundaryGenerator
    boundaryWallPc.unlit = 1.0f;
    vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                        sizeof(boundaryWallPc), &boundaryWallPc);
    boundaryWallMesh_->bindAndDraw(frame.commandBuffer);

    // Shells/effects/debris below all use vertex-color-only shading like
    // boxes did, so switch back to the plain white material.
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &whiteMaterialSet_, 0, nullptr);
    for (const auto& shell : projectiles_) {
        Pipeline::PushConstants shellPc{};
        shellPc.model = shell.worldMatrix();
        // Metal casing highlight, same idea as the tank's own bare-metal
        // parts -- see Mesh::shell's comment on the mesh's brass/gunmetal
        // vertex color this is meant to catch a highlight on top of.
        shellPc.specularStrength = 0.4f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(shellPc), &shellPc);
        shellMesh_->bindAndDraw(frame.commandBuffer);
    }

    // Smoke puffs (muzzle blast + shell trail wisps) -- unlit, like the
    // impact flash/embers below, and alpha-blended via opacity() rather
    // than opaque like everything else here.
    for (const auto& puff : smokePuffs_) {
        Pipeline::PushConstants puffPc{};
        puffPc.model = puff.worldMatrix();
        puffPc.unlit = 1.0f;
        puffPc.opacity = puff.opacity();
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(puffPc), &puffPc);
        const Mesh& puffMesh = puff.dust ? *dustPuffMesh_ : *smokePuffMesh_;
        puffMesh.bindAndDraw(frame.commandBuffer);
    }

    for (const auto& effect : impactEffects_) {
        Pipeline::PushConstants effectPc{};
        effectPc.model = effect.worldMatrix();
        effectPc.unlit = 1.0f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(effectPc), &effectPc);
        flashMesh_->bindAndDraw(frame.commandBuffer);
    }

    for (const auto& particle : debris_) {
        Pipeline::PushConstants debrisPc{};
        debrisPc.model = particle.worldMatrix();
        debrisPc.unlit = particle.ember ? 1.0f : 0.0f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(debrisPc), &debrisPc);
        const Mesh& mesh = particle.ember ? *debrisEmberMesh_ : *debrisChunkMesh_;
        mesh.bindAndDraw(frame.commandBuffer);
    }
    vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         gpuTimestampPool_, timestampBase + 5);

    uint32_t aliveBoxCount = 0;
    for (const auto& box : boxes_) {
        if (box.alive) ++aliveBoxCount;
    }

    hud_->begin();

    // Project the tank's actual aim point (along its firing direction) into
    // screen space, rather than a fixed screen-center crosshair -- with a
    // third-person chase camera that looks at the tank rather than down the
    // barrel, screen center doesn't correspond to where a shot will go.
    glm::vec3 aimWorldPoint = tank_->muzzleWorldPosition() + tank_->aimDirection() * 25.0f;
    glm::vec4 aimClip = ubo.proj * ubo.view * glm::vec4(aimWorldPoint, 1.0f);
    if (aimClip.w > 0.01f) {
        glm::vec2 aimNDC = glm::vec2(aimClip.x, aimClip.y) / aimClip.w;
        constexpr float kCrosshairArm = 0.025f;
        constexpr float kCrosshairThickness = 0.004f;
        glm::vec3 white(1.0f);
        hud_->addQuad(aimNDC, {kCrosshairArm / aspect, kCrosshairThickness}, white);
        hud_->addQuad(aimNDC, {kCrosshairThickness / aspect, kCrosshairArm}, white);
    }

    constexpr float kTickHalf = 0.018f;
    constexpr float kTickSpacing = 0.05f;
    constexpr float kTicksStartX = -0.9f;
    constexpr float kTicksY = 0.85f;
    glm::vec3 tickColor(0.2f, 0.9f, 0.3f);
    for (uint32_t i = 0; i < aliveBoxCount; ++i) {
        float x = kTicksStartX + kTickSpacing * static_cast<float>(i);
        hud_->addQuad({x, kTicksY}, {kTickHalf / aspect, kTickHalf}, tickColor);
    }

    constexpr glm::vec2 kFpsRightEdge(0.95f, 0.85f);
    constexpr float kFpsDigitHalfHeight = 0.035f;
    glm::vec3 fpsColor(1.0f, 1.0f, 1.0f);
    addNumber(*hud_, static_cast<int>(fpsSmoothed_ + 0.5f), kFpsRightEdge, kFpsDigitHalfHeight, aspect,
              fpsColor);

    vkCmdEndRendering(frame.commandBuffer);
    vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         gpuTimestampPool_, timestampBase + 6);

    // HudRenderer's pipeline declares a single color attachment, which is
    // incompatible with the 2-color-attachment scope above -- end that scope
    // and start a fresh one-color-attachment scope for it. The color image
    // stays in COLOR_ATTACHMENT_OPTIMAL throughout (no layout change), but
    // still needs an execution/memory barrier ordering the 3D pass's writes
    // before the HUD pass's; the history image transitions to
    // SHADER_READ_ONLY_OPTIMAL here too, ready to be the *other* slot's
    // input starting next frame.
    VkImageMemoryBarrier2 betweenScenesBarriers[] = {
        imageBarrier(colorImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
        imageBarrier(historyWriteImage, VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_2_SHADER_READ_BIT),
    };
    VkDependencyInfo betweenScenesDepInfo{};
    betweenScenesDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    betweenScenesDepInfo.imageMemoryBarrierCount = 2;
    betweenScenesDepInfo.pImageMemoryBarriers = betweenScenesBarriers;
    vkCmdPipelineBarrier2(frame.commandBuffer, &betweenScenesDepInfo);

    VkRenderingAttachmentInfo hudColorAttachment{};
    hudColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    hudColorAttachment.imageView = swapchain_->imageView(imageIndex);
    hudColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    hudColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    hudColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo hudRenderingInfo{};
    hudRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    hudRenderingInfo.renderArea = {{0, 0}, extent};
    hudRenderingInfo.layerCount = 1;
    hudRenderingInfo.colorAttachmentCount = 1;
    hudRenderingInfo.pColorAttachments = &hudColorAttachment;

    vkCmdBeginRendering(frame.commandBuffer, &hudRenderingInfo);
    vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
    hud_->render(frame.commandBuffer);

    vkCmdEndRendering(frame.commandBuffer);

    // Screenshot capture: copy the fully-composited frame (terrain, HUD,
    // everything) to a host-visible buffer while it's still ours, before
    // the present transition hands it to the presentation engine -- there's
    // no way to read it back afterward. See ScreenshotRequest's comment on
    // why this reads GPU memory directly rather than using an OS screenshot
    // tool.
    VkImageLayout colorLayoutBeforePresent = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (captureScreenshot) {
        VkDeviceSize byteCount = VkDeviceSize(extent.width) * extent.height * 4;
        screenshotBuffer = std::make_unique<Buffer>(*context_, byteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkImageMemoryBarrier2 toTransferSrc = imageBarrier(
            colorImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
        VkDependencyInfo toTransferSrcDepInfo{};
        toTransferSrcDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        toTransferSrcDepInfo.imageMemoryBarrierCount = 1;
        toTransferSrcDepInfo.pImageMemoryBarriers = &toTransferSrc;
        vkCmdPipelineBarrier2(frame.commandBuffer, &toTransferSrcDepInfo);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(frame.commandBuffer, colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                screenshotBuffer->handle(), 1, &region);

        colorLayoutBeforePresent = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }

    VkImageMemoryBarrier2 toPresent = imageBarrier(
        colorImage, VK_IMAGE_ASPECT_COLOR_BIT, colorLayoutBeforePresent, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        captureScreenshot ? VK_PIPELINE_STAGE_2_TRANSFER_BIT : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        captureScreenshot ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);

    VkDependencyInfo presentDepInfo{};
    presentDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    presentDepInfo.imageMemoryBarrierCount = 1;
    presentDepInfo.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(frame.commandBuffer, &presentDepInfo);

    vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         gpuTimestampPool_, timestampBase + 7);

    VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

    VkSemaphoreSubmitInfo waitSemaphoreInfo{};
    waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSemaphoreInfo.semaphore = frame.imageAvailable;
    waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphore renderFinished = swapchain_->renderFinishedSemaphore(imageIndex);

    VkSemaphoreSubmitInfo signalSemaphoreInfo{};
    signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSemaphoreInfo.semaphore = renderFinished;
    signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkCommandBufferSubmitInfo cmdBufferInfo{};
    cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdBufferInfo.commandBuffer = frame.commandBuffer;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdBufferInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

    VK_CHECK(vkQueueSubmit2(context_->graphicsQueue(), 1, &submitInfo, frame.inFlight));
    gpuTimestampsReady_[currentFrame_] = true;

    if (captureScreenshot) {
        // Screenshots are rare, explicit (CLI- or key-triggered) events, not
        // a per-frame cost -- stalling here for this one submission to
        // finish, rather than threading a multi-frame-latent readback
        // through the normal frame-pacing path, keeps the whole feature
        // contained to this one block.
        VK_CHECK(vkWaitForFences(context_->device(), 1, &frame.inFlight, VK_TRUE, UINT64_MAX));

        std::vector<uint8_t> pixels(static_cast<size_t>(extent.width) * extent.height * 4);
        screenshotBuffer->copyDataOut(pixels.data(), pixels.size());
        ScreenshotWriter::write(screenshotRequest_->path, pixels.data(), extent.width, extent.height,
                                 swapchain_->imageFormat());
        std::cout << "Saved screenshot to " << screenshotRequest_->path << std::endl;

        bool exitAfter = screenshotRequest_->exitAfter;
        screenshotRequest_.reset();
        if (exitAfter) glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    VkSwapchainKHR swapchains[] = {swapchain_->handle()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(context_->presentQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR ||
        framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapchainDependentResources();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("failed to present swapchain image");
    }

    currentFrame_ = (currentFrame_ + 1) % CommandContext::kFramesInFlight;
}
