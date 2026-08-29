#pragma once

#include <cstdint>
#include <vector>

#include "CommandContext.h"
#include "VulkanContext.h"

// RAII-owned sampled 2D texture (image + view + sampler), uploaded once from
// CPU-side RGBA8 pixel data via a staging buffer -- the same
// staging-then-copy pattern as Buffer::uploadDeviceLocal, just for an image
// instead of a buffer.
class Texture {
public:
    static Texture fromPixels(VulkanContext& ctx, CommandContext& commands, uint32_t width,
                               uint32_t height, const std::vector<uint8_t>& rgba8, bool repeat);

    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    VkImageView view() const { return view_; }
    VkSampler sampler() const { return sampler_; }

private:
    Texture() = default;
    void destroy();

    VulkanContext* ctx_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
};
