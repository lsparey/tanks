#include "Buffer.h"

#include <cstring>
#include <stdexcept>
#include <utility>

#include "VulkanCheck.h"
#include "VulkanUtils.h"

Buffer::Buffer(VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
               VkMemoryPropertyFlags properties)
    : ctx_(&ctx), size_(size) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(ctx_->device(), &bufferInfo, nullptr, &buffer_));

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(ctx_->device(), buffer_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(ctx_->physicalDevice(), memRequirements.memoryTypeBits, properties);

    VK_CHECK(vkAllocateMemory(ctx_->device(), &allocInfo, nullptr, &memory_));
    VK_CHECK(vkBindBufferMemory(ctx_->device(), buffer_, memory_, 0));
}

Buffer::~Buffer() { destroy(); }

Buffer::Buffer(Buffer&& other) noexcept
    : ctx_(other.ctx_), buffer_(other.buffer_), memory_(other.memory_), size_(other.size_) {
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.size_ = 0;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        destroy();
        ctx_ = other.ctx_;
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        size_ = other.size_;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.size_ = 0;
    }
    return *this;
}

void Buffer::destroy() {
    if (buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(ctx_->device(), buffer_, nullptr);
    if (memory_ != VK_NULL_HANDLE) vkFreeMemory(ctx_->device(), memory_, nullptr);
    buffer_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
}

void Buffer::copyData(const void* data, VkDeviceSize size) {
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(ctx_->device(), memory_, 0, size, 0, &mapped));
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(ctx_->device(), memory_);
}

Buffer Buffer::uploadDeviceLocal(VulkanContext& ctx, CommandContext& commands, const void* data,
                                 VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer staging(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.copyData(data, size);

    Buffer result(ctx, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commands.pool();
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(ctx.device(), &allocInfo, &cmd));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, staging.handle(), result.handle(), 1, &copyRegion);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(ctx.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(ctx.graphicsQueue()));

    vkFreeCommandBuffers(ctx.device(), commands.pool(), 1, &cmd);

    return result;
}
