#pragma once

#include "VulkanContext.h"

// Single exposure/tonemap/display-encoding pass. Samples the linear HDR
// scene-color target (see HdrTarget) with a fullscreen triangle and writes
// the tonemapped result into the swapchain's sRGB presentable image, in the
// same position the main 3D pass's MSAA resolve used to write directly --
// right before the HUD pass draws on top of it. Self-contained
// pipeline/layout, the same shape as HudRenderer, plus the one descriptor
// HudRenderer doesn't need: a sampler reading the HDR source image.
class TonemapPass {
public:
    TonemapPass(VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat);
    ~TonemapPass();

    TonemapPass(const TonemapPass&) = delete;
    TonemapPass& operator=(const TonemapPass&) = delete;

    // Call once at startup and again after every HdrTarget::recreate (its
    // image view changes on swapchain resize).
    void updateSourceDescriptor(VkImageView hdrView, VkSampler hdrSampler);
    void render(VkCommandBuffer cmd);

private:
    VulkanContext& ctx_;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
