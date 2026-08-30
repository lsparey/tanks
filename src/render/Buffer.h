#pragma once

#include "CommandContext.h"
#include "VulkanContext.h"

// RAII wrapper around a VkBuffer + its backing VkDeviceMemory. Move-only so
// there's exactly one owner responsible for destruction.
class Buffer {
public:
    Buffer(VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
           VkMemoryPropertyFlags properties);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    VkBuffer handle() const { return buffer_; }
    VkDeviceSize size() const { return size_; }
    // Only valid for buffers created with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    // (uploadDeviceLocal always sets this, for acceleration-structure builds).
    VkDeviceAddress deviceAddress() const;

    // Only valid for host-visible buffers.
    void copyData(const void* data, VkDeviceSize size);

    // Uploads `data` into a new device-local buffer via a staging buffer and
    // a one-time transfer command, then blocks until the copy completes.
    // Used for vertex/index buffers, which are written once at load time and
    // read many times per frame by the GPU.
    static Buffer uploadDeviceLocal(VulkanContext& ctx, CommandContext& commands,
                                     const void* data, VkDeviceSize size,
                                     VkBufferUsageFlags usage);

private:
    void destroy();

    VulkanContext* ctx_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
};
