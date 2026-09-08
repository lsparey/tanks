#pragma once

#include "TreeShadowCascades.h"
#include "VulkanContext.h"

// One 48 MiB array shared by the already serialized scene submissions.
// The explicit read/write barriers also order reuse on the graphics queue.
class TreeShadowMap {
public:
    explicit TreeShadowMap(VulkanContext& ctx);
    ~TreeShadowMap();
    TreeShadowMap(const TreeShadowMap&) = delete;
    TreeShadowMap& operator=(const TreeShadowMap&) = delete;
    bool initialized() const { return initialized_; }
    VkImageView view() const { return view_; }
    VkSampler sampler() const { return sampler_; }
    void begin(VkCommandBuffer cmd);
    void beginCascade(VkCommandBuffer cmd, uint32_t cascade);
    void end(VkCommandBuffer cmd);
private:
    void destroy();
    VulkanContext& ctx_;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    std::array<VkImageView, TreeShadowCascades::kCount> layers_{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    bool initialized_ = false;
};
