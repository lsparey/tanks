#pragma once

#include "VulkanContext.h"

// Single, non-ping-ponged linear HDR scene-color target. The main 3D pass's
// MSAA color attachment resolves into this every frame, and TonemapPass
// samples it immediately afterward in the same frame -- unlike
// HistoryBuffer, there's no cross-frame accumulation here, so one resolve
// image plus its own MSAA scratch is enough: no ping-pong, and no startup
// clear/transition is needed either, since every frame's LOAD_OP_CLEAR
// fully overwrites it before it's ever sampled (the same UNDEFINED-is-fine
// reasoning Application::drawFrame already applies to the swapchain's own
// resolve targets).
//
// Sized to the swapchain; recreate() must be called (and TonemapPass's
// source descriptor rewritten) whenever the swapchain resizes, since that
// creates new VkImage/VkImageView objects.
class HdrTarget {
public:
    static constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    HdrTarget(VulkanContext& ctx, VkExtent2D extent);
    ~HdrTarget();

    HdrTarget(const HdrTarget&) = delete;
    HdrTarget& operator=(const HdrTarget&) = delete;

    void recreate(VkExtent2D extent);

    VkFormat format() const { return kFormat; }
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
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;

    VkImage msaaImage_ = VK_NULL_HANDLE;
    VkDeviceMemory msaaMemory_ = VK_NULL_HANDLE;
    VkImageView msaaImageView_ = VK_NULL_HANDLE;
};
