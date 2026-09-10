#include "ResolveTarget.h"

#include "VulkanCheck.h"
#include "VulkanUtils.h"

ResolveTarget::ResolveTarget(VulkanContext& ctx, VkExtent2D extent, VkFormat format)
    : ctx_(ctx), format_(format) {
    create(extent);
}

void ResolveTarget::create(VkExtent2D extent) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VK_CHECK(vkCreateSampler(ctx_.device(), &samplerInfo, nullptr, &sampler_));

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateImage(ctx_.device(), &imageInfo, nullptr, &image_));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(ctx_.device(), image_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(ctx_.physicalDevice(), memRequirements.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(ctx_.device(), &allocInfo, nullptr, &memory_));
    VK_CHECK(vkBindImageMemory(ctx_.device(), image_, memory_, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format_;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(ctx_.device(), &viewInfo, nullptr, &imageView_));

    // Single-sample rendering draws straight into image_ (no resolve), so
    // the multisampled scratch image would never be touched -- skip it.
    if (ctx_.msaaSamples() == VK_SAMPLE_COUNT_1_BIT) return;

    VkImageCreateInfo msaaImageInfo = imageInfo;
    msaaImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    msaaImageInfo.samples = ctx_.msaaSamples();
    VK_CHECK(vkCreateImage(ctx_.device(), &msaaImageInfo, nullptr, &msaaImage_));

    VkMemoryRequirements msaaMemRequirements;
    vkGetImageMemoryRequirements(ctx_.device(), msaaImage_, &msaaMemRequirements);

    VkMemoryAllocateInfo msaaAllocInfo{};
    msaaAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    msaaAllocInfo.allocationSize = msaaMemRequirements.size;
    msaaAllocInfo.memoryTypeIndex = findMemoryType(
        ctx_.physicalDevice(), msaaMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(ctx_.device(), &msaaAllocInfo, nullptr, &msaaMemory_));
    VK_CHECK(vkBindImageMemory(ctx_.device(), msaaImage_, msaaMemory_, 0));

    VkImageViewCreateInfo msaaViewInfo{};
    msaaViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    msaaViewInfo.image = msaaImage_;
    msaaViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    msaaViewInfo.format = format_;
    msaaViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(ctx_.device(), &msaaViewInfo, nullptr, &msaaImageView_));
}

void ResolveTarget::destroy() {
    if (msaaImageView_ != VK_NULL_HANDLE) vkDestroyImageView(ctx_.device(), msaaImageView_, nullptr);
    if (msaaImage_ != VK_NULL_HANDLE) vkDestroyImage(ctx_.device(), msaaImage_, nullptr);
    if (msaaMemory_ != VK_NULL_HANDLE) vkFreeMemory(ctx_.device(), msaaMemory_, nullptr);
    msaaImageView_ = VK_NULL_HANDLE;
    msaaImage_ = VK_NULL_HANDLE;
    msaaMemory_ = VK_NULL_HANDLE;

    if (imageView_ != VK_NULL_HANDLE) vkDestroyImageView(ctx_.device(), imageView_, nullptr);
    if (image_ != VK_NULL_HANDLE) vkDestroyImage(ctx_.device(), image_, nullptr);
    if (memory_ != VK_NULL_HANDLE) vkFreeMemory(ctx_.device(), memory_, nullptr);
    imageView_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;

    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(ctx_.device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
}

void ResolveTarget::recreate(VkExtent2D extent) {
    destroy();
    create(extent);
}

ResolveTarget::~ResolveTarget() { destroy(); }
