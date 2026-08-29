#pragma once

#include <array>

#include "VulkanContext.h"

// Owns the graphics command pool and per-frame-in-flight command buffers and
// synchronization primitives (image-available/render-finished semaphores and
// an in-flight fence).
class CommandContext {
public:
    static constexpr int kFramesInFlight = 2;

    // renderFinished is deliberately not here -- it must be indexed by
    // swapchain image, not frame-in-flight (see Swapchain::renderFinishedSemaphore).
    struct FrameData {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    explicit CommandContext(VulkanContext& ctx);
    ~CommandContext();

    CommandContext(const CommandContext&) = delete;
    CommandContext& operator=(const CommandContext&) = delete;

    FrameData& frame(size_t index) { return frames_[index]; }

    // Exposed so one-off resource uploads (e.g. Buffer's staging copy) can
    // allocate transient command buffers from the same pool instead of
    // creating their own.
    VkCommandPool pool() const { return commandPool_; }

private:
    VulkanContext& ctx_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::array<FrameData, kFramesInFlight> frames_{};
};
