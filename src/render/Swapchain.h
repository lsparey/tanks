#pragma once

#include <vector>

#include "VulkanContext.h"

// Owns the swapchain, its image views, and a matching depth buffer. Handles
// resize by destroying and recreating everything against the current
// framebuffer size.
class Swapchain {
public:
    Swapchain(VulkanContext& ctx, GLFWwindow* window);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void recreate();

    VkSwapchainKHR handle() const { return swapchain_; }
    VkExtent2D extent() const { return extent_; }
    VkFormat imageFormat() const { return imageFormat_; }
    VkFormat depthFormat() const { return depthFormat_; }
    size_t imageCount() const { return images_.size(); }
    VkImage image(size_t i) const { return images_[i]; }
    VkImageView imageView(size_t i) const { return imageViews_[i]; }
    VkImage depthImage() const { return depthImage_; }
    VkImageView depthImageView() const { return depthImageView_; }

    // Indexed by swapchain image index, NOT frame-in-flight index: present
    // operations aren't gated by the per-frame-in-flight fence, so reusing a
    // frame-indexed semaphore here can signal it while a previous present is
    // still consuming it. See:
    // https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
    VkSemaphore renderFinishedSemaphore(size_t imageIndex) const {
        return renderFinishedSemaphores_[imageIndex];
    }

private:
    void create();
    void createImageViews();
    void createDepthResources();
    void createSyncObjects();
    void destroy();

    VulkanContext& ctx_;
    GLFWwindow* window_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    VkFormat imageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};

    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    std::vector<VkSemaphore> renderFinishedSemaphores_;
};
