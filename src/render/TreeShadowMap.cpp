#include "TreeShadowMap.h"
#include "VulkanCheck.h"
#include "VulkanUtils.h"

TreeShadowMap::TreeShadowMap(VulkanContext& ctx) : ctx_(ctx) {
    try {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(ctx_.physicalDevice(), VK_FORMAT_D32_SFLOAT, &properties);
        constexpr auto required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) != required)
            throw std::runtime_error("tree shadows require sampled D32 depth attachments");
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_D32_SFLOAT;
        info.extent = {TreeShadowCascades::kResolution, TreeShadowCascades::kResolution, 1};
        info.mipLevels = 1;
        info.arrayLayers = TreeShadowCascades::kCount;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VK_CHECK(vkCreateImage(ctx_.device(), &info, nullptr, &image_));
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(ctx_.device(), image_, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = findMemoryType(ctx_.physicalDevice(), requirements.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(ctx_.device(), &allocation, nullptr, &memory_));
        VK_CHECK(vkBindImageMemory(ctx_.device(), image_, memory_, 0));
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = image_;
        view.format = info.format;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, TreeShadowCascades::kCount};
        VK_CHECK(vkCreateImageView(ctx_.device(), &view, nullptr, &view_));
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.subresourceRange.layerCount = 1;
        for (uint32_t i=0; i<TreeShadowCascades::kCount; ++i) {
            view.subresourceRange.baseArrayLayer = i;
            VK_CHECK(vkCreateImageView(ctx_.device(), &view, nullptr, &layers_[i]));
        }
        // Manual bilinear depth comparisons in GLSL avoid requiring linear
        // filtering support on D32 and allow PCSS to read blocker depths.
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK(vkCreateSampler(ctx_.device(), &sampler, nullptr, &sampler_));
    } catch (...) { destroy(); throw; }
}

TreeShadowMap::~TreeShadowMap() { destroy(); }
void TreeShadowMap::destroy() {
    if (sampler_) vkDestroySampler(ctx_.device(), sampler_, nullptr);
    for (auto layer : layers_) if (layer) vkDestroyImageView(ctx_.device(), layer, nullptr);
    if (view_) vkDestroyImageView(ctx_.device(), view_, nullptr);
    if (image_) vkDestroyImage(ctx_.device(), image_, nullptr);
    if (memory_) vkFreeMemory(ctx_.device(), memory_, nullptr);
}

void TreeShadowMap::begin(VkCommandBuffer cmd) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.srcAccessMask = initialized_ ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = initialized_ ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,TreeShadowCascades::kCount};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

void TreeShadowMap::beginCascade(VkCommandBuffer cmd, uint32_t cascade) {
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = layers_.at(cascade);
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1.f,0};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0,0},{TreeShadowCascades::kResolution,TreeShadowCascades::kResolution}};
    rendering.layerCount = 1;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &rendering);
    // Positive viewport: sampling uses NDC.xy * .5 + .5 for this map only.
    VkViewport viewport{0,0,float(TreeShadowCascades::kResolution),float(TreeShadowCascades::kResolution),0,1};
    vkCmdSetViewport(cmd,0,1,&viewport);
    vkCmdSetScissor(cmd,0,1,&rendering.renderArea);
}

void TreeShadowMap::end(VkCommandBuffer cmd) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,TreeShadowCascades::kCount};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd,&dependency);
    initialized_ = true;
}
