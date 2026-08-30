#pragma once

#include <array>
#include <memory>
#include <vector>

#include "AccelerationStructure.h"
#include "CommandContext.h"
#include "VulkanContext.h"

// Owns one TLAS per frame-in-flight slot, each reserved for up to
// kMaxInstances instances at construction. Rebuilt every frame (see
// AccelerationStructure::recordRebuildTLAS) from the current scene's full
// instance list -- terrain + trees (static) plus tank parts, boxes, shells
// (changing every frame) -- recorded directly into that frame's own command
// buffer, so it never blocks waiting on the GPU the way a fresh
// buildTLAS-from-scratch-and-wait every frame would.
//
// Rebuilding this frame-in-flight slot's TLAS is safe because
// Application::drawFrame already waits on this slot's inFlight fence (in
// CommandContext) before recording anything into its command buffer -- so
// by the time rebuild() runs, the GPU is guaranteed done with whatever this
// slot's TLAS held last time it was this slot's turn.
class SceneAccelerationStructure {
public:
    // Bumped from 128 as rock clusters, tree bark, and other static detail
    // grew the typical/worst-case instance count -- cheap to reserve extra
    // headroom (each slot is a fixed-size instance buffer sized at
    // construction) rather than risk silently dropping instances under
    // AccelerationStructure::recordRebuildTLAS's clamp-to-capacity.
    static constexpr uint32_t kMaxInstances = 256;

    SceneAccelerationStructure(VulkanContext& ctx, CommandContext& commands,
                                const std::vector<AccelerationStructure::Instance>& initialInstances);

    void rebuild(VkCommandBuffer cmd, size_t frameIndex,
                 const std::vector<AccelerationStructure::Instance>& instances);

    VkAccelerationStructureKHR handle(size_t frameIndex) const { return slots_[frameIndex]->handle(); }

private:
    std::array<std::unique_ptr<AccelerationStructure>, CommandContext::kFramesInFlight> slots_;
};
