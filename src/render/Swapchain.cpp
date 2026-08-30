#include "Swapchain.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "VulkanCheck.h"
#include "VulkanUtils.h"

namespace {

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

SwapchainSupport querySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupport support;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &support.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    support.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, support.formats.data());

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    support.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount,
                                               support.presentModes.data());
    return support;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats[0];
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& /*modes*/) {
    // FIFO is guaranteed to be available and gives stable, vsynced frame
    // pacing, which is all this prototype needs.
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    VkExtent2D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                               capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                                capabilities.maxImageExtent.height);
    return extent;
}

}  // namespace

Swapchain::Swapchain(VulkanContext& ctx, GLFWwindow* window) : ctx_(ctx), window_(window) {
    create();
    createImageViews();
    createDepthResources();
    createColorResources();
    createSyncObjects();
}

Swapchain::~Swapchain() { destroy(); }

void Swapchain::recreate() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window_, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(ctx_.device());
    destroy();
    create();
    createImageViews();
    createDepthResources();
    createColorResources();
    createSyncObjects();
}

void Swapchain::create() {
    SwapchainSupport support = querySwapchainSupport(ctx_.physicalDevice(), ctx_.surface());
    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    VkExtent2D extent = chooseExtent(support.capabilities, window_);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = ctx_.surface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = {ctx_.graphicsQueueFamily(), ctx_.presentQueueFamily()};
    if (ctx_.graphicsQueueFamily() != ctx_.presentQueueFamily()) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(ctx_.device(), &createInfo, nullptr, &swapchain_));

    imageFormat_ = surfaceFormat.format;
    extent_ = extent;

    uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(ctx_.device(), swapchain_, &actualImageCount, nullptr);
    images_.resize(actualImageCount);
    vkGetSwapchainImagesKHR(ctx_.device(), swapchain_, &actualImageCount, images_.data());
}

void Swapchain::createImageViews() {
    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = imageFormat_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(ctx_.device(), &viewInfo, nullptr, &imageViews_[i]));
    }
}

void Swapchain::createDepthResources() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {extent_.width, extent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = ctx_.msaaSamples();
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(ctx_.device(), &imageInfo, nullptr, &depthImage_));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(ctx_.device(), depthImage_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(ctx_.physicalDevice(), memRequirements.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vkAllocateMemory(ctx_.device(), &allocInfo, nullptr, &depthImageMemory_));
    VK_CHECK(vkBindImageMemory(ctx_.device(), depthImage_, depthImageMemory_, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(ctx_.device(), &viewInfo, nullptr, &depthImageView_));
}

void Swapchain::createColorResources() {
    // The presentable swapchain images are always single-sample (that's a
    // WSI requirement), so when MSAA is active the pipeline can't render
    // directly into them -- it renders into this multisampled scratch image
    // instead, which the driver then resolves (averages down) into the
    // presentable image at the end of the render pass. If the device has no
    // MSAA support, ctx_.msaaSamples() is VK_SAMPLE_COUNT_1_BIT and this is
    // just a redundant same-format 1-sample image; harmless, not worth a
    // special case.
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {extent_.width, extent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = imageFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.samples = ctx_.msaaSamples();
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(ctx_.device(), &imageInfo, nullptr, &colorImage_));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(ctx_.device(), colorImage_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(ctx_.physicalDevice(), memRequirements.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vkAllocateMemory(ctx_.device(), &allocInfo, nullptr, &colorImageMemory_));
    VK_CHECK(vkBindImageMemory(ctx_.device(), colorImage_, colorImageMemory_, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = colorImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(ctx_.device(), &viewInfo, nullptr, &colorImageView_));
}

void Swapchain::createSyncObjects() {
    renderFinishedSemaphores_.resize(images_.size());
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& semaphore : renderFinishedSemaphores_) {
        VK_CHECK(vkCreateSemaphore(ctx_.device(), &semaphoreInfo, nullptr, &semaphore));
    }
}

void Swapchain::destroy() {
    for (auto semaphore : renderFinishedSemaphores_) {
        vkDestroySemaphore(ctx_.device(), semaphore, nullptr);
    }
    renderFinishedSemaphores_.clear();
    if (colorImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx_.device(), colorImageView_, nullptr);
        colorImageView_ = VK_NULL_HANDLE;
    }
    if (colorImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(ctx_.device(), colorImage_, nullptr);
        colorImage_ = VK_NULL_HANDLE;
    }
    if (colorImageMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(ctx_.device(), colorImageMemory_, nullptr);
        colorImageMemory_ = VK_NULL_HANDLE;
    }
    if (depthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx_.device(), depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(ctx_.device(), depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthImageMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(ctx_.device(), depthImageMemory_, nullptr);
        depthImageMemory_ = VK_NULL_HANDLE;
    }
    for (auto view : imageViews_) {
        vkDestroyImageView(ctx_.device(), view, nullptr);
    }
    imageViews_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx_.device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}
