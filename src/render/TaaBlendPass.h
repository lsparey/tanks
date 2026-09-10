#pragma once

#include <array>

#include "CommandContext.h"
#include "VulkanContext.h"

// Basic temporal anti-aliasing resolve: blends the current frame's linear
// HDR color (see ResolveTarget/HdrTarget) with a reprojected, neighborhood-
// clamped sample of last frame's blended result (see HistoryBuffer), using
// the resolved velocity buffer to find the reprojection UV. Writes this
// frame's blended result into the *other* ping-pong slot of that same
// HistoryBuffer, for TonemapPass to read and for next frame's blend to read
// as history in turn.
//
// Deliberately simple: a fixed history blend weight and a 4-tap (not 3x3)
// neighborhood clamp, no variance clipping or motion-blurred neighborhoods
// -- see PLAN.md's "Linear HDR and temporal image stability" for why.
// Self-contained pipeline/layout, the same shape as TonemapPass, plus a
// third sampled input (history) and a second push-constant flag.
class TaaBlendPass {
public:
    TaaBlendPass(VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat);
    ~TaaBlendPass();

    TaaBlendPass(const TaaBlendPass&) = delete;
    TaaBlendPass& operator=(const TaaBlendPass&) = delete;

    // All descriptors are configured once at startup and again after every
    // ResolveTarget/HistoryBuffer recreate (their image views change on
    // swapchain resize) -- NEVER per frame. There is one descriptor set per
    // frame in flight: the history binding's read slot alternates with the
    // ping-pong, and rewriting a single shared set from drawFrame would
    // race the other in-flight frame's command buffer still using it on the
    // GPU (spec-illegal, and in practice the previous frame's blend can end
    // up sampling the very slot it is concurrently writing -- visible
    // intermittent glitching). Same pattern as Pipeline's per-frame history
    // descriptor sets.
    void updateSourceDescriptor(VkImageView hdrView, VkSampler hdrSampler,
                                 VkImageView velocityView, VkSampler velocitySampler);
    // Writes frameIndex's set only; call for every frame index with that
    // frame's history *read* slot view.
    void updateHistoryDescriptor(size_t frameIndex, VkImageView historyView,
                                 VkSampler historySampler);
    void render(VkCommandBuffer cmd, size_t frameIndex, bool historyValid, bool taaEnabled);

private:
    VulkanContext& ctx_;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, CommandContext::kFramesInFlight> descriptorSets_{};
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
