#include "HistoryBuffer.h"

#include "CommandContext.h"
#include "SingleTimeCommands.h"
#include "VulkanCheck.h"
#include "VulkanUtils.h"

namespace {

void transitionAndClear(VulkanContext& ctx, CommandContext& commands, VkImage image) {
    VkCommandBuffer cmd = beginSingleTimeCommands(ctx, commands);

    VkImageMemoryBarrier toTransferDst{};
    toTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.image = image;
    toTransferDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                          nullptr, 0, nullptr, 1, &toTransferDst);

    // Clear to (1.0 "fully lit, no shadow", 1.0 "no AO occlusion", a huge
    // distance, unused) so the very first frame -- before any real temporal
    // data exists -- doesn't start from garbage memory, and the huge
    // distance guarantees the depth-based disocclusion check in basic.frag
    // always (safely) rejects this placeholder data rather than blending
    // with it. 50000, not something larger: the format is
    // R16G16B16A16_SFLOAT (half precision, max ~65504), so a value like 1e6
    // would silently become infinity.
    VkClearColorValue clearValue{};
    clearValue.float32[0] = 1.0f;
    clearValue.float32[1] = 1.0f;
    clearValue.float32[2] = 50000.0f;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);

    VkImageMemoryBarrier toShaderRead{};
    toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image = image;
    toShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &toShaderRead);

    endSingleTimeCommands(ctx, commands, cmd);
}

}  // namespace

HistoryBuffer::HistoryBuffer(VulkanContext& ctx, CommandContext& commands, VkExtent2D extent)
    : ctx_(ctx) {
    create(extent);
    for (size_t i = 0; i < kSlotCount; ++i) {
        transitionAndClear(ctx_, commands, images_[i]);
    }
}

void HistoryBuffer::create(VkExtent2D extent) {
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

    for (size_t i = 0; i < kSlotCount; ++i) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format_;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateImage(ctx_.device(), &imageInfo, nullptr, &images_[i]));

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(ctx_.device(), images_[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(ctx_.physicalDevice(), memRequirements.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(ctx_.device(), &allocInfo, nullptr, &memory_[i]));
        VK_CHECK(vkBindImageMemory(ctx_.device(), images_[i], memory_[i], 0));

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(ctx_.device(), &viewInfo, nullptr, &imageViews_[i]));
    }

    VkImageCreateInfo msaaImageInfo{};
    msaaImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    msaaImageInfo.imageType = VK_IMAGE_TYPE_2D;
    msaaImageInfo.extent = {extent.width, extent.height, 1};
    msaaImageInfo.mipLevels = 1;
    msaaImageInfo.arrayLayers = 1;
    msaaImageInfo.format = format_;
    msaaImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    msaaImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    msaaImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    msaaImageInfo.samples = ctx_.msaaSamples();
    msaaImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
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

void HistoryBuffer::destroy() {
    if (msaaImageView_ != VK_NULL_HANDLE) vkDestroyImageView(ctx_.device(), msaaImageView_, nullptr);
    if (msaaImage_ != VK_NULL_HANDLE) vkDestroyImage(ctx_.device(), msaaImage_, nullptr);
    if (msaaMemory_ != VK_NULL_HANDLE) vkFreeMemory(ctx_.device(), msaaMemory_, nullptr);
    msaaImageView_ = VK_NULL_HANDLE;
    msaaImage_ = VK_NULL_HANDLE;
    msaaMemory_ = VK_NULL_HANDLE;

    for (size_t i = 0; i < kSlotCount; ++i) {
        if (imageViews_[i] != VK_NULL_HANDLE) vkDestroyImageView(ctx_.device(), imageViews_[i], nullptr);
        if (images_[i] != VK_NULL_HANDLE) vkDestroyImage(ctx_.device(), images_[i], nullptr);
        if (memory_[i] != VK_NULL_HANDLE) vkFreeMemory(ctx_.device(), memory_[i], nullptr);
        imageViews_[i] = VK_NULL_HANDLE;
        images_[i] = VK_NULL_HANDLE;
        memory_[i] = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(ctx_.device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
}

void HistoryBuffer::recreate(CommandContext& commands, VkExtent2D extent) {
    destroy();
    create(extent);
    for (size_t i = 0; i < kSlotCount; ++i) {
        transitionAndClear(ctx_, commands, images_[i]);
    }
}

HistoryBuffer::~HistoryBuffer() { destroy(); }
