#include "Application.h"

#include <algorithm>
#include <iostream>
#include <random>

#include <glm/gtc/matrix_transform.hpp>

#include "../render/VulkanCheck.h"
#include "../scene/CollisionSystem.h"
#include "../scene/GrassTextureGenerator.h"
#include "../scene/RockTextureGenerator.h"
#include "../scene/TrackTextureGenerator.h"

namespace {

constexpr uint32_t kWindowWidth = 1280;
constexpr uint32_t kWindowHeight = 720;

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

Application::Application() {
    initWindow();
    context_ = std::make_unique<VulkanContext>(window_);
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

    std::vector<uint8_t> grassPixels = GrassTextureGenerator::generate(256);
    grassTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 256, 256, grassPixels, /*repeat=*/true));
    std::vector<uint8_t> rockPixels = RockTextureGenerator::generate(256);
    rockTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 256, 256, rockPixels, /*repeat=*/true));
    std::vector<uint8_t> trackPixels = TrackTextureGenerator::generate(128);
    trackTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 128, 128, trackPixels, /*repeat=*/false));
    std::vector<uint8_t> whitePixel = {255, 255, 255, 255};
    whiteTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 1, 1, whitePixel, /*repeat=*/false));
    // Terrain blends grass and rock by height (see heightBlend in
    // PushConstants/basic.frag); everything else just binds white into both
    // slots since only the primary is ever sampled for them.
    terrainMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*grassTexture_, *rockTexture_);
    trackMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*trackTexture_, *trackTexture_);
    whiteMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*whiteTexture_, *whiteTexture_);

    uint32_t terrainSeed = std::random_device{}();
    terrain_ = std::make_unique<Terrain>(*context_, *commands_, /*resolution=*/64,
                                          /*worldSize=*/60.0f, /*amplitude=*/3.0f, terrainSeed);
    tank_ = std::make_unique<Tank>(*context_, *commands_,
                                    std::string(ASSET_ROOT) + "/assets/models/tank.x");
    boxMesh_ = std::make_unique<Mesh>(Mesh::cube(*context_, *commands_, glm::vec3(0.65f, 0.5f, 0.25f)));
    shellMesh_ = std::make_unique<Mesh>(Mesh::cube(*context_, *commands_, glm::vec3(1.0f, 0.85f, 0.2f)));
    flashMesh_ = std::make_unique<Mesh>(Mesh::cube(*context_, *commands_, glm::vec3(1.0f, 1.0f, 0.9f)));
    treeMesh_ = std::make_unique<Mesh>(
        Mesh::tree(*context_, *commands_, glm::vec3(0.32f, 0.22f, 0.12f), glm::vec3(0.13f, 0.32f, 0.10f)));
    // Explosion debris: a darker, splintered-looking chunk of the box
    // (normally lit, so it tumbles through the scene's light/shadow like
    // real debris) and a small, bright unlit ember (a spark/fire glow that
    // ignores lighting entirely) -- see DebrisParticle and spawnExplosion.
    debrisChunkMesh_ =
        std::make_unique<Mesh>(Mesh::cube(*context_, *commands_, glm::vec3(0.32f, 0.22f, 0.12f)));
    debrisEmberMesh_ =
        std::make_unique<Mesh>(Mesh::cube(*context_, *commands_, glm::vec3(1.0f, 0.55f, 0.1f)));
    // White so the track texture's own baked-in brown color shows through
    // unmodified (same reasoning as terrain's kTerrainColor).
    trackMarkMesh_ = std::make_unique<Mesh>(Mesh::quad(*context_, *commands_, glm::vec3(1.0f)));
    spawnBoxes();
    spawnTrees();
    buildAccelerationStructures();
    input_ = std::make_unique<InputManager>(window_);
    lastFrameTime_ = glfwGetTime();
}

Application::~Application() {
    if (context_) vkDeviceWaitIdle(context_->device());

    // Destroy in dependency order before the GLFW window disappears.
    historyBuffer_.reset();
    sceneAS_.reset();
    shellBLAS_.reset();
    boxBLAS_.reset();
    treeBLAS_.reset();
    whiteTexture_.reset();
    trackTexture_.reset();
    rockTexture_.reset();
    grassTexture_.reset();
    hud_.reset();
    trackMarkMesh_.reset();
    debrisEmberMesh_.reset();
    debrisChunkMesh_.reset();
    treeMesh_.reset();
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
    window_ = glfwCreateWindow(kWindowWidth, kWindowHeight, "luke-game", nullptr, nullptr);
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

        input_->update();

        bool fDown = glfwGetKey(window_, GLFW_KEY_F) == GLFW_PRESS;
        if (fDown && !prevFKeyDown_) followTank_ = !followTank_;
        prevFKeyDown_ = fDown;

        tank_->update(*input_, deltaTime, *terrain_);
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
    constexpr float kEdgeMargin = 6.0f;          // keep boxes off the very edge of the terrain
    constexpr float kMinDistanceFromSpawn = 10.0f;  // tank starts at the origin
    constexpr float kMinDistanceBetweenBoxes = 6.0f;
    constexpr int kMaxAttemptsPerBox = 50;

    std::mt19937 rng(std::random_device{}());
    float half = terrain_->worldSize() * 0.5f - kEdgeMargin;
    std::uniform_real_distribution<float> coordDist(-half, half);

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
        boxes_.push_back(box);
    }
}

void Application::spawnTrees() {
    constexpr int kTreeCount = 40;
    constexpr float kEdgeMargin = 3.0f;
    constexpr float kMinDistanceFromSpawn = 8.0f;
    constexpr float kMinDistanceBetweenTrees = 3.0f;
    constexpr int kMaxAttemptsPerTree = 30;

    std::mt19937 rng(std::random_device{}());
    float half = terrain_->worldSize() * 0.5f - kEdgeMargin;
    std::uniform_real_distribution<float> coordDist(-half, half);
    std::uniform_real_distribution<float> yawDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> scaleDist(0.8f, 1.4f);

    std::vector<glm::vec2> placed;
    for (int i = 0; i < kTreeCount; ++i) {
        glm::vec2 pos{0.0f, 0.0f};
        for (int attempt = 0; attempt < kMaxAttemptsPerTree; ++attempt) {
            glm::vec2 candidate(coordDist(rng), coordDist(rng));
            bool tooCloseToSpawn = glm::length(candidate) < kMinDistanceFromSpawn;
            bool tooCloseToOther = std::any_of(placed.begin(), placed.end(), [&](glm::vec2 p) {
                return glm::length(p - candidate) < kMinDistanceBetweenTrees;
            });
            pos = candidate;
            if (!tooCloseToSpawn && !tooCloseToOther) break;
        }
        placed.push_back(pos);

        TreeInstance tree;
        tree.position = glm::vec3(pos.x, terrain_->heightAt(pos.x, pos.y), pos.y);
        tree.yaw = yawDist(rng);
        tree.scale = scaleDist(rng);
        trees_.push_back(tree);
    }
}

void Application::spawnExplosion(glm::vec3 position) {
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

void Application::updateTrackMarks(float deltaTime) {
    // Drop a new mark roughly every kTrackMarkSpacing units of travel --
    // well under TrackMark's own length (1.8) so consecutive marks overlap
    // generously and merge into one continuous strip instead of a chain of
    // separate blobs with visible gaps between them. The anchor is the
    // position of the last mark dropped, not the tank's own start position,
    // so this also naturally stops spawning while the tank is stationary.
    constexpr float kTrackMarkSpacing = 0.55f;
    glm::vec3 pos = tank_->position();
    if (!hasTrackMarkAnchor_ || glm::length(pos - lastTrackMarkPosition_) >= kTrackMarkSpacing) {
        // Tilt to the local terrain normal (instead of assuming flat ground)
        // so the decal hugs sloped terrain rather than poking through it;
        // project the tank's forward onto that normal's tangent plane the
        // same way Tank itself derives forward_ from flatForward and up_.
        glm::vec3 normal = terrain_->normalAt(pos.x, pos.z);
        glm::vec3 rawForward = tank_->forward();
        glm::vec3 projectedForward =
            glm::normalize(rawForward - normal * glm::dot(rawForward, normal));

        TrackMark mark;
        mark.position = glm::vec3(pos.x, terrain_->heightAt(pos.x, pos.z) + 0.05f, pos.z);
        mark.up = normal;
        mark.forward = projectedForward;
        mark.width = tank_->hullWidth();
        trackMarks_.push_back(mark);
        lastTrackMarkPosition_ = pos;
        hasTrackMarkAnchor_ = true;
    }

    for (auto& mark : trackMarks_) mark.update(deltaTime);
    trackMarks_.erase(std::remove_if(trackMarks_.begin(), trackMarks_.end(),
                                      [](const TrackMark& m) { return !m.alive; }),
                       trackMarks_.end());
}

void Application::buildAccelerationStructures() {
    treeBLAS_ = std::make_unique<AccelerationStructure>(
        AccelerationStructure::buildBLAS(*context_, *commands_, *treeMesh_));
    boxBLAS_ = std::make_unique<AccelerationStructure>(
        AccelerationStructure::buildBLAS(*context_, *commands_, *boxMesh_));
    shellBLAS_ = std::make_unique<AccelerationStructure>(
        AccelerationStructure::buildBLAS(*context_, *commands_, *shellMesh_));

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
        instances.push_back({treeBLAS_->deviceAddress(), tree.worldMatrix()});
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
    // and invisible anyway) shadows.
    return instances;
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
}

void Application::updateProjectilesAndCollisions(float deltaTime) {
    for (auto& shell : projectiles_) {
        if (!shell.alive) continue;
        shell.update(deltaTime);
        for (auto& box : boxes_) {
            if (!box.alive) continue;
            float t = 0.0f;
            if (CollisionSystem::segmentIntersectsAABB(shell.previousPosition, shell.position,
                                                         box.aabbMin(), box.aabbMax(), &t)) {
                box.alive = false;
                shell.alive = false;
                glm::vec3 hitPoint = glm::mix(shell.previousPosition, shell.position, t);
                ImpactEffect effect;
                effect.position = hitPoint;
                impactEffects_.push_back(effect);
                spawnExplosion(hitPoint);
                break;
            }
        }
    }

    projectiles_.erase(
        std::remove_if(projectiles_.begin(), projectiles_.end(),
                        [](const Projectile& p) { return !p.alive; }),
        projectiles_.end());

    for (auto& effect : impactEffects_) effect.update(deltaTime);
    impactEffects_.erase(
        std::remove_if(impactEffects_.begin(), impactEffects_.end(),
                        [](const ImpactEffect& e) { return !e.alive; }),
        impactEffects_.end());

    for (auto& particle : debris_) particle.update(deltaTime);
    debris_.erase(std::remove_if(debris_.begin(), debris_.end(),
                                  [](const DebrisParticle& d) { return !d.alive; }),
                  debris_.end());
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
    pipeline_->updateFrameUBO(ubo);
    prevViewProj_ = ubo.proj * ubo.view;
    prevCameraPos_ = camera_.position();
    ++frameCounter_;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    // Rebuild this frame-in-flight slot's TLAS from the current scene state
    // (acceleration structure builds can't happen inside a dynamic
    // rendering scope, so this must be before vkCmdBeginRendering). Read by
    // basic.frag via ray query for shadow tracing.
    sceneAS_->rebuild(frame.commandBuffer, currentFrame_, gatherRayTracingInstances());

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

    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &terrainMaterialSet_, 0, nullptr);
    Pipeline::PushConstants terrainPc{};
    terrainPc.model = glm::mat4(1.0f);
    terrainPc.heightBlend = 1.0f;
    vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                        sizeof(terrainPc), &terrainPc);
    terrain_->bindAndDraw(frame.commandBuffer);

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
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(markPc), &markPc);
        trackMarkMesh_->bindAndDraw(frame.commandBuffer);
    }

    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &whiteMaterialSet_, 0, nullptr);
    for (const auto& part : tank_->drawParts()) {
        Pipeline::PushConstants tankPc{};
        tankPc.model = part.worldMatrix;
        tankPc.specularStrength = 0.6f;
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(tankPc), &tankPc);
        part.mesh->bindAndDraw(frame.commandBuffer);
    }

    for (const auto& box : boxes_) {
        if (!box.alive) continue;
        Pipeline::PushConstants boxPc{};
        boxPc.model = box.worldMatrix();
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(boxPc), &boxPc);
        boxMesh_->bindAndDraw(frame.commandBuffer);
    }

    for (const auto& tree : trees_) {
        Pipeline::PushConstants treePc{};
        treePc.model = tree.worldMatrix();
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(treePc), &treePc);
        treeMesh_->bindAndDraw(frame.commandBuffer);
    }

    for (const auto& shell : projectiles_) {
        Pipeline::PushConstants shellPc{};
        shellPc.model = shell.worldMatrix();
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(shellPc), &shellPc);
        shellMesh_->bindAndDraw(frame.commandBuffer);
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

    uint32_t aliveBoxCount = 0;
    for (const auto& box : boxes_) {
        if (box.alive) ++aliveBoxCount;
    }

    hud_->begin();

    // Project the tank's actual aim point (along its firing direction) into
    // screen space, rather than a fixed screen-center crosshair -- with a
    // third-person chase camera that looks at the tank rather than down the
    // barrel, screen center doesn't correspond to where a shot will go.
    glm::vec3 aimWorldPoint = tank_->position() + tank_->aimDirection() * 25.0f +
                               glm::vec3(0.0f, 1.3f, 0.0f);
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

    vkCmdEndRendering(frame.commandBuffer);

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

    VkImageMemoryBarrier2 toPresent = imageBarrier(
        colorImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);

    VkDependencyInfo presentDepInfo{};
    presentDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    presentDepInfo.imageMemoryBarrierCount = 1;
    presentDepInfo.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(frame.commandBuffer, &presentDepInfo);

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
