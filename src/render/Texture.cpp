#include "Texture.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Buffer.h"
#include "SingleTimeCommands.h"
#include "VulkanCheck.h"
#include "VulkanUtils.h"

Texture Texture::fromPixels(VulkanContext& ctx, CommandContext& commands, uint32_t width,
                             uint32_t height, const std::vector<uint8_t>& rgba8, bool repeat) {
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    Buffer staging(ctx, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.copyData(rgba8.data(), imageSize);

    Texture tex;
    tex.ctx_ = &ctx;

    // Full mip chain down to 1x1 -- these are tiled repeatedly across large
    // surfaces (terrain in particular), so without mips, minification at
    // distance/grazing angles aliases into visible shimmer as the terrain
    // scrolls; trilinear filtering (see sampler below) needs the chain to
    // actually blend between. R8G8B8A8_UNORM linear-blit support is
    // universal enough on Vulkan-capable hardware not to bother querying for
    // it here.
    uint32_t mipLevels =
        static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateImage(ctx.device(), &imageInfo, nullptr, &tex.image_));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(ctx.device(), tex.image_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(ctx.physicalDevice(), memRequirements.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(ctx.device(), &allocInfo, nullptr, &tex.memory_));
    VK_CHECK(vkBindImageMemory(ctx.device(), tex.image_, tex.memory_, 0));

    VkCommandBuffer cmd = beginSingleTimeCommands(ctx, commands);

    auto mipBarrier = [&](uint32_t level, VkImageLayout oldLayout, VkImageLayout newLayout,
                          VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                          VkPipelineStageFlags dstStage, uint32_t levelCount = 1) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tex.image_;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, levelCount, 0, 1};
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    };

    // Every level starts out TRANSFER_DST_OPTIMAL: level 0 is about to
    // receive the actual pixels, and every level above it is about to
    // receive a blit from the level below (see the mip-chain loop).
    mipBarrier(0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, mipLevels);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, staging.handle(), tex.image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1, &region);

    // Successively blit each level down into the next (box filter via
    // VK_FILTER_LINEAR) -- level i-1 has to become a blit *source* first,
    // then goes to SHADER_READ_ONLY once nothing will blit from it again;
    // level i stays TRANSFER_DST_OPTIMAL, becoming the next iteration's
    // source in turn.
    int32_t mipWidth = static_cast<int32_t>(width);
    int32_t mipHeight = static_cast<int32_t>(height);
    for (uint32_t level = 1; level < mipLevels; ++level) {
        mipBarrier(level - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
        vkCmdBlitImage(cmd, tex.image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex.image_,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        mipBarrier(level - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    // The last level was only ever a blit destination (or, if there's just
    // one level, the original copy destination) -- either way it's still
    // TRANSFER_DST_OPTIMAL and hasn't been transitioned yet.
    mipBarrier(mipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    endSingleTimeCommands(ctx, commands, cmd);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
    VK_CHECK(vkCreateImageView(ctx.device(), &viewInfo, nullptr, &tex.view_));

    VkSamplerAddressMode addressMode =
        repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    // Grazing-angle views of tiled ground textures (a low chase camera over
    // flat-ish terrain is exactly this case) minify far more along one axis
    // than the other -- plain trilinear still has to pick a single, more
    // conservative mip for the whole footprint and ends up blurrier than
    // necessary. Anisotropic sampling corrects for that.
    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(ctx.physicalDevice(), &deviceProps);
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = std::min(8.0f, deviceProps.limits.maxSamplerAnisotropy);
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    // Trilinear: blends between mip levels as well as within one, so
    // minification doesn't visibly pop between mips as the camera moves.
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    VK_CHECK(vkCreateSampler(ctx.device(), &samplerInfo, nullptr, &tex.sampler_));

    return tex;
}

Texture::~Texture() { destroy(); }

Texture::Texture(Texture&& other) noexcept
    : ctx_(other.ctx_),
      image_(other.image_),
      memory_(other.memory_),
      view_(other.view_),
      sampler_(other.sampler_) {
    other.image_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.view_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        destroy();
        ctx_ = other.ctx_;
        image_ = other.image_;
        memory_ = other.memory_;
        view_ = other.view_;
        sampler_ = other.sampler_;
        other.image_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.view_ = VK_NULL_HANDLE;
        other.sampler_ = VK_NULL_HANDLE;
    }
    return *this;
}

void Texture::destroy() {
    if (!ctx_) return;
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(ctx_->device(), sampler_, nullptr);
    if (view_ != VK_NULL_HANDLE) vkDestroyImageView(ctx_->device(), view_, nullptr);
    if (image_ != VK_NULL_HANDLE) vkDestroyImage(ctx_->device(), image_, nullptr);
    if (memory_ != VK_NULL_HANDLE) vkFreeMemory(ctx_->device(), memory_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
}
