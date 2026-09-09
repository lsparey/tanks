#include "SceneAccelerationStructure.h"

#include <algorithm>

SceneAccelerationStructure::SceneAccelerationStructure(
    VulkanContext& ctx, CommandContext& commands,
    const std::vector<AccelerationStructure::Instance>& initialInstances) {
    for (size_t i = 0; i < slots_.size(); ++i) {
        slots_[i] = std::make_unique<AccelerationStructure>(
            AccelerationStructure::buildTLAS(ctx, commands, initialInstances, kMaxInstances));
        instances_[i] = initialInstances;
    }
}

bool SceneAccelerationStructure::rebuild(VkCommandBuffer cmd, size_t frameIndex,
                                          const std::vector<AccelerationStructure::Instance>& instances) {
    auto& previous = instances_[frameIndex];
    if (previous.size() == instances.size() && std::equal(previous.begin(), previous.end(), instances.begin(),
        [](const auto& a, const auto& b) {
            return a.blasAddress == b.blasAddress && a.mask == b.mask && a.transform == b.transform;
        })) return false;
    slots_[frameIndex]->recordRebuildTLAS(cmd, instances);
    previous = instances;
    return true;
}
