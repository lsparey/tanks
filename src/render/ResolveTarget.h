#pragma once

#include "VulkanContext.h"

// Single, non-ping-ponged full-screen render target: an MSAA color
// attachment resolves into this every frame, and something samples it
// immediately afterward in the same frame -- unlike HistoryBuffer, there's
// no cross-frame accumulation here, so one resolve image plus its own MSAA
// scratch is enough: no ping-pong, and no startup clear/transition is needed
// either, since every frame's LOAD_OP_CLEAR fully overwrites it before it's
// ever sampled (the same UNDEFINED-is-fine reasoning Application::drawFrame
// already applies to the swapchain's own resolve targets).
//
// Used for both the linear HDR scene-color target (sampled once by
// TonemapPass) and the resolved velocity/motion-vector target (sampled by
// TonemapPass's debug visualization today, the future TAA blend pass
// eventually) -- same shape, different format.
//
// Sized to the swapchain; recreate() must be called (and any dependent
// descriptor rewritten) whenever the swapchain resizes, since that creates
// new VkImage/VkImageView objects.
class ResolveTarget {
public:
    ResolveTarget(VulkanContext& ctx, VkExtent2D extent, VkFormat format);
    ~ResolveTarget();

    ResolveTarget(const ResolveTarget&) = delete;
    ResolveTarget& operator=(const ResolveTarget&) = delete;

    void recreate(VkExtent2D extent);

    VkFormat format() const { return format_; }
    VkImage image() const { return image_; }
    VkImageView imageView() const { return imageView_; }
    VkSampler sampler() const { return sampler_; }

    // Multisampled scratch write target -- see HistoryBuffer::msaaImage()
    // for the identical pattern (the pipeline renders into this, and the
    // driver resolves it down into imageView() above at the end of the
    // main scene pass).
    VkImage msaaImage() const { return msaaImage_; }
    VkImageView msaaImageView() const { return msaaImageView_; }

private:
    void create(VkExtent2D extent);
    void destroy();

    VulkanContext& ctx_;
    VkFormat format_;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;

    VkImage msaaImage_ = VK_NULL_HANDLE;
    VkDeviceMemory msaaMemory_ = VK_NULL_HANDLE;
    VkImageView msaaImageView_ = VK_NULL_HANDLE;
};
