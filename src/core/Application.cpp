#include "Application.h"

#include <algorithm>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include "../render/VulkanCheck.h"
#include "../scene/CollisionSystem.h"
#include "../scene/GrassTextureGenerator.h"

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
    pipeline_ = std::make_unique<Pipeline>(*context_, swapchain_->imageFormat(),
                                            swapchain_->depthFormat());
    hud_ = std::make_unique<HudRenderer>(*context_, swapchain_->imageFormat(),
                                          swapchain_->depthFormat());

    std::vector<uint8_t> grassPixels = GrassTextureGenerator::generate(256);
    grassTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 256, 256, grassPixels, /*repeat=*/true));
    std::vector<uint8_t> whitePixel = {255, 255, 255, 255};
    whiteTexture_ = std::make_unique<Texture>(
        Texture::fromPixels(*context_, *commands_, 1, 1, whitePixel, /*repeat=*/false));
    grassMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*grassTexture_);
    whiteMaterialSet_ = pipeline_->allocateMaterialDescriptorSet(*whiteTexture_);

    terrain_ = std::make_unique<Terrain>(*context_, *commands_, /*resolution=*/64,
                                          /*worldSize=*/60.0f, /*amplitude=*/3.0f);
    tank_ = std::make_unique<Tank>(*context_, *commands_,
                                    std::string(ASSET_ROOT) + "/assets/models/tank.x");
    boxMesh_ = std::make_unique<Mesh>(Mesh::cube(*context_, *commands_, glm::vec3(0.65f, 0.5f, 0.25f)));
    shellMesh_ = std::make_unique<Mesh>(Mesh::cube(*context_, *commands_, glm::vec3(1.0f, 0.85f, 0.2f)));
    flashMesh_ = std::make_unique<Mesh>(Mesh::cube(*context_, *commands_, glm::vec3(1.0f, 1.0f, 0.9f)));
    spawnBoxes();
    input_ = std::make_unique<InputManager>(window_);
    lastFrameTime_ = glfwGetTime();
}

Application::~Application() {
    if (context_) vkDeviceWaitIdle(context_->device());

    // Destroy in dependency order before the GLFW window disappears.
    whiteTexture_.reset();
    grassTexture_.reset();
    hud_.reset();
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

        bool fireDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
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
    const float positions[][2] = {{8, 6}, {-10, 4}, {5, -12}, {-6, -8}, {14, -3}, {-14, -14},
                                   {2, 16}, {-4, 12}};
    for (const auto& p : positions) {
        Box box;
        box.size = 2.0f;
        box.position = glm::vec3(p[0], terrain_->heightAt(p[0], p[1]) + box.size * 0.5f, p[1]);
        boxes_.push_back(box);
    }
}

void Application::fireProjectile() {
    // Measured from the model's own vertex data: the barrel tip is a small
    // centered cluster at local (x~0, y~1.3, z~-3.37) -- see Tank::worldMatrix
    // for why local -Z is the barrel end.
    constexpr float kMuzzleForwardOffset = 3.3f;
    constexpr float kMuzzleHeightOffset = 1.3f;
    constexpr float kShellSpeed = 25.0f;

    Projectile shell;
    shell.position = tank_->position() + tank_->forward() * kMuzzleForwardOffset +
                      glm::vec3(0.0f, kMuzzleHeightOffset, 0.0f);
    shell.previousPosition = shell.position;
    shell.velocity = tank_->forward() * kShellSpeed;
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
                ImpactEffect effect;
                effect.position = glm::mix(shell.previousPosition, shell.position, t);
                impactEffects_.push_back(effect);
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
}

void Application::drawFrame() {
    auto& frame = commands_->frame(currentFrame_);

    VK_CHECK(vkWaitForFences(context_->device(), 1, &frame.inFlight, VK_TRUE, UINT64_MAX));

    uint32_t imageIndex = 0;
    VkResult acquireResult =
        vkAcquireNextImageKHR(context_->device(), swapchain_->handle(), UINT64_MAX,
                               frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_->recreate();
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
    ubo.lightDir = glm::vec4(glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)), 0.0f);
    ubo.cameraPos = glm::vec4(camera_.position(), 0.0f);
    pipeline_->updateFrameUBO(ubo);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    VkImage colorImage = swapchain_->image(imageIndex);

    // Both attachments are fully overwritten this frame (LOAD_OP_CLEAR), so
    // treating oldLayout as UNDEFINED is correct regardless of prior layout:
    // it tells the driver not to preserve contents, matching the clear.
    VkImageMemoryBarrier2 toAttachments[] = {
        imageBarrier(colorImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
        imageBarrier(swapchain_->depthImage(), VK_IMAGE_ASPECT_DEPTH_BIT,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
    };

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 2;
    depInfo.pImageMemoryBarriers = toAttachments;
    vkCmdPipelineBarrier2(frame.commandBuffer, &depInfo);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchain_->imageView(imageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.45f, 0.65f, 0.85f, 1.0f}};

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
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
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

    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &grassMaterialSet_, 0, nullptr);
    Pipeline::PushConstants terrainPc{};
    terrainPc.model = glm::mat4(1.0f);
    vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                        sizeof(terrainPc), &terrainPc);
    terrain_->bindAndDraw(frame.commandBuffer);

    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_->layout(), 1, 1, &whiteMaterialSet_, 0, nullptr);
    Pipeline::PushConstants tankPc{};
    tankPc.model = tank_->worldMatrix();
    tankPc.specularStrength = 0.6f;
    vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                        sizeof(tankPc), &tankPc);
    tank_->bindAndDraw(frame.commandBuffer);

    for (const auto& box : boxes_) {
        if (!box.alive) continue;
        Pipeline::PushConstants boxPc{};
        boxPc.model = box.worldMatrix();
        vkCmdPushConstants(frame.commandBuffer, pipeline_->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(boxPc), &boxPc);
        boxMesh_->bindAndDraw(frame.commandBuffer);
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

    uint32_t aliveBoxCount = 0;
    for (const auto& box : boxes_) {
        if (box.alive) ++aliveBoxCount;
    }

    hud_->begin();

    // Project the tank's actual aim point (along its firing direction) into
    // screen space, rather than a fixed screen-center crosshair -- with a
    // third-person chase camera that looks at the tank rather than down the
    // barrel, screen center doesn't correspond to where a shot will go.
    glm::vec3 aimWorldPoint = tank_->position() + tank_->forward() * 25.0f +
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
        swapchain_->recreate();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("failed to present swapchain image");
    }

    currentFrame_ = (currentFrame_ + 1) % CommandContext::kFramesInFlight;
}
