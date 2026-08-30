#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Buffer.h"
#include "CommandContext.h"
#include "Mesh.h"
#include "VulkanContext.h"

// RAII wrapper around a VkAccelerationStructureKHR + its backing buffer,
// move-only (mirrors Buffer's style).
//
// buildBLAS/buildTLAS build synchronously -- their own one-time command
// buffer + wait via SingleTimeCommands -- which is fine for a one-off,
// built-once resource (static-scene BLAS/TLAS from M1, and a TLAS's first
// build). recordRebuildTLAS instead re-records a fresh build into an
// ALREADY-OPEN command buffer with no wait of its own, reusing this TLAS's
// existing backing/scratch/instance buffers -- this is what lets the
// per-frame dynamic-scene TLAS (M2) update every frame without a
// vkQueueWaitIdle-per-frame stall that would defeat frames-in-flight.
class AccelerationStructure {
public:
    ~AccelerationStructure();
    AccelerationStructure(const AccelerationStructure&) = delete;
    AccelerationStructure& operator=(const AccelerationStructure&) = delete;
    AccelerationStructure(AccelerationStructure&& other) noexcept;
    AccelerationStructure& operator=(AccelerationStructure&& other) noexcept;

    VkAccelerationStructureKHR handle() const { return handle_; }
    VkDeviceAddress deviceAddress() const { return address_; }

    // Bottom-level: built once from a mesh's existing vertex/index buffers
    // (no separate copy of the geometry).
    static AccelerationStructure buildBLAS(VulkanContext& ctx, CommandContext& commands,
                                            const Mesh& mesh);

    struct Instance {
        VkDeviceAddress blasAddress;
        glm::mat4 transform;  // world matrix, same as used for rasterization
    };

    // Top-level: built from a list of BLAS-address + world-transform pairs.
    // If reserveCapacity > instances.size(), the backing/scratch/instance
    // buffers are sized for that many instances instead, so a later
    // recordRebuildTLAS can grow up to that count without reallocating.
    static AccelerationStructure buildTLAS(VulkanContext& ctx, CommandContext& commands,
                                            const std::vector<Instance>& instances,
                                            uint32_t reserveCapacity = 0);

    // Re-records a full rebuild (not a refit) of this TLAS using new
    // instance data, into `cmd` (already begun elsewhere -- typically the
    // frame's main command buffer, before vkCmdBeginRendering). Does not
    // submit or wait. instances.size() must not exceed the capacity this
    // TLAS was created with via buildTLAS's reserveCapacity.
    void recordRebuildTLAS(VkCommandBuffer cmd, const std::vector<Instance>& instances);

private:
    AccelerationStructure() = default;
    void destroy();

    // Shared by buildBLAS/buildTLAS: query build sizes (using
    // maxPrimitiveCount, so buffers are sized for future growth up to that
    // many), create the backing buffer + AS object, create a scratch
    // buffer, record+submit a build of actualPrimitiveCount primitives.
    static AccelerationStructure buildFromGeometry(VulkanContext& ctx, CommandContext& commands,
                                                    VkAccelerationStructureTypeKHR type,
                                                    const VkAccelerationStructureGeometryKHR& geometry,
                                                    uint32_t maxPrimitiveCount,
                                                    uint32_t actualPrimitiveCount);

    VulkanContext* ctx_ = nullptr;
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    VkDeviceAddress address_ = 0;
    std::unique_ptr<Buffer> buffer_;          // AS backing storage
    std::unique_ptr<Buffer> scratch_;         // kept alive for reuse by recordRebuildTLAS
    std::unique_ptr<Buffer> instanceBuffer_;  // TLAS only, host-visible, reused across rebuilds
    uint32_t capacity_ = 0;                   // TLAS only: max instances the buffers were sized for
};
