#include "CommandContext.h"

#include "VulkanCheck.h"

CommandContext::CommandContext(VulkanContext& ctx) : ctx_(ctx) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = ctx_.graphicsQueueFamily();
    VK_CHECK(vkCreateCommandPool(ctx_.device(), &poolInfo, nullptr, &commandPool_));

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (auto& frame : frames_) {
        VK_CHECK(vkAllocateCommandBuffers(ctx_.device(), &allocInfo, &frame.commandBuffer));
        VK_CHECK(vkCreateSemaphore(ctx_.device(), &semaphoreInfo, nullptr, &frame.imageAvailable));
        VK_CHECK(vkCreateFence(ctx_.device(), &fenceInfo, nullptr, &frame.inFlight));
    }
}

CommandContext::~CommandContext() {
    for (auto& frame : frames_) {
        if (frame.imageAvailable != VK_NULL_HANDLE)
            vkDestroySemaphore(ctx_.device(), frame.imageAvailable, nullptr);
        if (frame.inFlight != VK_NULL_HANDLE) vkDestroyFence(ctx_.device(), frame.inFlight, nullptr);
    }
    if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(ctx_.device(), commandPool_, nullptr);
}
