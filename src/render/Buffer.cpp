#include "Buffer.h"

#include <cstring>
#include <stdexcept>
#include <utility>

#include "SingleTimeCommands.h"
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

    // Buffers used as acceleration-structure build inputs need their device
    // address queryable, which requires memory allocated with this flag set.
    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(ctx_->physicalDevice(), memRequirements.memoryTypeBits, properties);
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        allocInfo.pNext = &allocFlagsInfo;
    }

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

VkDeviceAddress Buffer::deviceAddress() const {
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer_;
    return vkGetBufferDeviceAddress(ctx_->device(), &info);
}

void Buffer::copyData(const void* data, VkDeviceSize size) {
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(ctx_->device(), memory_, 0, size, 0, &mapped));
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(ctx_->device(), memory_);
}

void Buffer::copyDataOut(void* dst, VkDeviceSize size) const {
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(ctx_->device(), memory_, 0, size, 0, &mapped));
    std::memcpy(dst, mapped, static_cast<size_t>(size));
    vkUnmapMemory(ctx_->device(), memory_);
}

Buffer Buffer::uploadDeviceLocal(VulkanContext& ctx, CommandContext& commands, const void* data,
                                 VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer staging(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.copyData(data, size);

    // Every device-local buffer gets acceleration-structure-build-input +
    // device-address usage unconditionally, rather than threading a new
    // per-call-site bool through uploadDeviceLocal -- cheap on modern
    // drivers, and every vertex/index buffer in the game is a potential
    // BLAS input now that ray tracing is in the picture.
    Buffer result(ctx, size,
                  usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkCommandBuffer cmd = beginSingleTimeCommands(ctx, commands);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, staging.handle(), result.handle(), 1, &copyRegion);

    endSingleTimeCommands(ctx, commands, cmd);

    return result;
}
