#pragma once

#include <array>

#include "CommandContext.h"
#include "VulkanContext.h"

// Two ping-ponged four-channel (R16G16B16A16_SFLOAT: shadow value, AO
// value, view-distance, unused) images used for temporal accumulation of
// the noisy multi-sample shadow/AO terms computed in basic.frag. The
// view-distance channel (measured from the camera at write time) lets the
// shader reject reprojected samples whose stored distance doesn't match
// what the current fragment's world position should measure -- i.e. a
// different surface was there last frame (disocclusion), not the same one
// persisting -- instead of blending in stale data; both the shadow and AO
// channels share this one validity check since they're read from the same
// reprojected pixel. Each frame writes its result into slot (frameIndex %
// 2) as a second color attachment (MRT) and reads the OTHER slot -- last
// frame's result -- as a sampled texture input, ping-ponging naturally in
// lockstep with the existing 2 frames-in-flight.
//
// Sized to the swapchain; recreate() must be called (and the sampled-image
// descriptor sets rewritten) whenever the swapchain resizes, since that
// creates new VkImage/VkImageView objects.
class HistoryBuffer {
public:
    static constexpr size_t kSlotCount = 2;

    HistoryBuffer(VulkanContext& ctx, CommandContext& commands, VkExtent2D extent);
    ~HistoryBuffer();

    HistoryBuffer(const HistoryBuffer&) = delete;
    HistoryBuffer& operator=(const HistoryBuffer&) = delete;

    void recreate(CommandContext& commands, VkExtent2D extent);

    VkFormat format() const { return format_; }
    VkImage image(size_t slot) const { return images_[slot]; }
    VkImageView imageView(size_t slot) const { return imageViews_[slot]; }
    VkSampler sampler() const { return sampler_; }

    // Multisampled scratch write target (see Swapchain::colorImage() for the
    // same pattern) -- the pipeline renders history into this, and the
    // driver resolves it down into images_[frameIndex] at the end of the
    // render pass. Not ping-ponged: it's fully transient within a single
    // frame, so both frame-in-flight slots can share the one scratch image.
    VkImage msaaImage() const { return msaaImage_; }
    VkImageView msaaImageView() const { return msaaImageView_; }

private:
    void create(VkExtent2D extent);
    void destroy();

    VulkanContext& ctx_;
    VkFormat format_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkSampler sampler_ = VK_NULL_HANDLE;

    std::array<VkImage, kSlotCount> images_{};
    std::array<VkDeviceMemory, kSlotCount> memory_{};
    std::array<VkImageView, kSlotCount> imageViews_{};

    VkImage msaaImage_ = VK_NULL_HANDLE;
    VkDeviceMemory msaaMemory_ = VK_NULL_HANDLE;
    VkImageView msaaImageView_ = VK_NULL_HANDLE;
};
