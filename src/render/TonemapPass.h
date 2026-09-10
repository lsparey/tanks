#pragma once

#include <array>

#include "CommandContext.h"
#include "VulkanContext.h"

// Single exposure/tonemap/display-encoding pass. Samples the linear HDR
// scene-color target (see ResolveTarget) with a fullscreen triangle and
// writes the tonemapped result into the swapchain's sRGB presentable image,
// in the same position the main 3D pass's MSAA resolve used to write
// directly -- right before the HUD pass draws on top of it. Self-contained
// pipeline/layout, the same shape as HudRenderer, plus the two descriptors
// HudRenderer doesn't need: samplers reading the HDR and velocity source
// images.
//
// Also carries a debug-only velocity visualization mode (F-key toggled, see
// Application), since this is the only practical way to confirm the new
// motion-vector buffer is correct before a real TAA blend pass exists to
// consume it -- see PLAN.md's "Linear HDR and temporal image stability".
class TonemapPass {
public:
    TonemapPass(VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat);
    ~TonemapPass();

    TonemapPass(const TonemapPass&) = delete;
    TonemapPass& operator=(const TonemapPass&) = delete;

    // Call at startup and again after every ResolveTarget/HistoryBuffer
    // recreate (their image views change on swapchain resize) -- NEVER per
    // frame. One descriptor set per frame in flight: the source binding
    // points at the TAA history slot that frame's blend pass writes, so it
    // differs per frame index and rewriting a single shared set from
    // drawFrame would race the other in-flight frame's command buffer (see
    // TaaBlendPass's header comment). Call updateSourceDescriptor for every
    // frame index with that frame's view.
    void updateSourceDescriptor(size_t frameIndex, VkImageView hdrView, VkSampler hdrSampler);
    void updateVelocityDescriptor(VkImageView velocityView, VkSampler velocitySampler);
    void render(VkCommandBuffer cmd, size_t frameIndex, bool showVelocityDebug);

private:
    VulkanContext& ctx_;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, CommandContext::kFramesInFlight> descriptorSets_{};
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
