#include "SceneAccelerationStructure.h"

SceneAccelerationStructure::SceneAccelerationStructure(
    VulkanContext& ctx, CommandContext& commands,
    const std::vector<AccelerationStructure::Instance>& initialInstances) {
    for (auto& slot : slots_) {
        slot = std::make_unique<AccelerationStructure>(
            AccelerationStructure::buildTLAS(ctx, commands, initialInstances, kMaxInstances));
    }
}

void SceneAccelerationStructure::rebuild(VkCommandBuffer cmd, size_t frameIndex,
                                          const std::vector<AccelerationStructure::Instance>& instances) {
    slots_[frameIndex]->recordRebuildTLAS(cmd, instances);
}
