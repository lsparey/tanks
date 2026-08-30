#include "AccelerationStructure.h"

#include <algorithm>

#include "RayTracingFunctions.h"
#include "SingleTimeCommands.h"
#include "VulkanCheck.h"

namespace {

// Vulkan's VkTransformMatrixKHR is a row-major 3x4 affine transform; glm::mat4
// is column-major, so this is a transpose (and a drop of the unused 4th row).
VkTransformMatrixKHR toVkTransform(const glm::mat4& m) {
    VkTransformMatrixKHR t{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            t.matrix[row][col] = m[col][row];
        }
    }
    return t;
}

VkAccelerationStructureInstanceKHR toVkInstance(const AccelerationStructure::Instance& instance,
                                                 uint32_t index) {
    VkAccelerationStructureInstanceKHR inst{};
    inst.transform = toVkTransform(instance.transform);
    inst.instanceCustomIndex = index;
    inst.mask = 0xFF;
    inst.instanceShaderBindingTableRecordOffset = 0;
    inst.flags = 0;
    inst.accelerationStructureReference = instance.blasAddress;
    return inst;
}

}  // namespace

AccelerationStructure AccelerationStructure::buildFromGeometry(
    VulkanContext& ctx, CommandContext& commands, VkAccelerationStructureTypeKHR type,
    const VkAccelerationStructureGeometryKHR& geometry, uint32_t maxPrimitiveCount,
    uint32_t actualPrimitiveCount) {
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = type;
    // ALLOW_DATA_ACCESS is required by VK_KHR_ray_tracing_position_fetch on
    // any acceleration structure whose triangle positions get fetched
    // (rayQueryGetIntersectionTriangleVertexPositionsEXT in basic.frag, for
    // RT reflections) -- without it, fetched positions are undefined by
    // spec. This function builds both BLAS (which need it) and the
    // one-time initial TLAS (which doesn't contain triangle geometry, so
    // the flag is simply irrelevant/harmless there); simpler to set it
    // unconditionally here than to thread a "needs position fetch" bool in.
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                       VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    pfnGetAccelerationStructureBuildSizesKHR(ctx.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &buildInfo, &maxPrimitiveCount, &sizeInfo);

    AccelerationStructure result;
    result.ctx_ = &ctx;
    result.buffer_ = std::make_unique<Buffer>(
        ctx, sizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = result.buffer_->handle();
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = type;
    VK_CHECK(pfnCreateAccelerationStructureKHR(ctx.device(), &createInfo, nullptr, &result.handle_));

    result.scratch_ = std::make_unique<Buffer>(
        ctx, sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    buildInfo.dstAccelerationStructure = result.handle_;
    buildInfo.scratchData.deviceAddress = result.scratch_->deviceAddress();

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = actualPrimitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* rangeInfos[] = {&rangeInfo};

    VkCommandBuffer cmd = beginSingleTimeCommands(ctx, commands);
    pfnCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, rangeInfos);
    endSingleTimeCommands(ctx, commands, cmd);

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = result.handle_;
    result.address_ = pfnGetAccelerationStructureDeviceAddressKHR(ctx.device(), &addressInfo);

    return result;
}

AccelerationStructure::~AccelerationStructure() { destroy(); }

AccelerationStructure::AccelerationStructure(AccelerationStructure&& other) noexcept
    : ctx_(other.ctx_),
      handle_(other.handle_),
      address_(other.address_),
      buffer_(std::move(other.buffer_)),
      scratch_(std::move(other.scratch_)),
      instanceBuffer_(std::move(other.instanceBuffer_)),
      capacity_(other.capacity_) {
    other.handle_ = VK_NULL_HANDLE;
    other.address_ = 0;
}

AccelerationStructure& AccelerationStructure::operator=(AccelerationStructure&& other) noexcept {
    if (this != &other) {
        destroy();
        ctx_ = other.ctx_;
        handle_ = other.handle_;
        address_ = other.address_;
        buffer_ = std::move(other.buffer_);
        scratch_ = std::move(other.scratch_);
        instanceBuffer_ = std::move(other.instanceBuffer_);
        capacity_ = other.capacity_;
        other.handle_ = VK_NULL_HANDLE;
        other.address_ = 0;
    }
    return *this;
}

void AccelerationStructure::destroy() {
    if (handle_ != VK_NULL_HANDLE) {
        pfnDestroyAccelerationStructureKHR(ctx_->device(), handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
    buffer_.reset();
    scratch_.reset();
    instanceBuffer_.reset();
}

AccelerationStructure AccelerationStructure::buildBLAS(VulkanContext& ctx, CommandContext& commands,
                                                        const Mesh& mesh) {
    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;  // matches Vertex::position
    triangles.vertexData.deviceAddress = mesh.vertexBuffer().deviceAddress();
    triangles.vertexStride = sizeof(Vertex);
    triangles.maxVertex = mesh.vertexCount() - 1;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = mesh.indexBuffer().deviceAddress();

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles = triangles;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    uint32_t triangleCount = mesh.indexCount() / 3;
    return buildFromGeometry(ctx, commands, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry,
                              triangleCount, triangleCount);
}

AccelerationStructure AccelerationStructure::buildTLAS(VulkanContext& ctx, CommandContext& commands,
                                                        const std::vector<Instance>& instances,
                                                        uint32_t reserveCapacity) {
    uint32_t capacity = std::max(reserveCapacity, static_cast<uint32_t>(instances.size()));

    auto instanceBuffer = std::make_unique<Buffer>(
        ctx, static_cast<VkDeviceSize>(capacity) * sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (!instances.empty()) {
        std::vector<VkAccelerationStructureInstanceKHR> instanceData(instances.size());
        for (size_t i = 0; i < instances.size(); ++i) {
            instanceData[i] = toVkInstance(instances[i], static_cast<uint32_t>(i));
        }
        instanceBuffer->copyData(instanceData.data(),
                                  sizeof(VkAccelerationStructureInstanceKHR) * instanceData.size());
    }

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;
    instancesData.data.deviceAddress = instanceBuffer->deviceAddress();

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    AccelerationStructure result =
        buildFromGeometry(ctx, commands, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, geometry, capacity,
                           static_cast<uint32_t>(instances.size()));
    result.instanceBuffer_ = std::move(instanceBuffer);
    result.capacity_ = capacity;
    return result;
}

void AccelerationStructure::recordRebuildTLAS(VkCommandBuffer cmd, const std::vector<Instance>& instances) {
    uint32_t count = static_cast<uint32_t>(instances.size());
    if (count > capacity_) {
        // Shouldn't happen at this project's scale (capacity is reserved
        // generously) -- clamp rather than write past the instance buffer.
        count = capacity_;
    }

    if (count > 0) {
        std::vector<VkAccelerationStructureInstanceKHR> instanceData(count);
        for (uint32_t i = 0; i < count; ++i) {
            instanceData[i] = toVkInstance(instances[i], i);
        }
        instanceBuffer_->copyData(instanceData.data(), sizeof(VkAccelerationStructureInstanceKHR) * count);
    }

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;
    instancesData.data.deviceAddress = instanceBuffer_->deviceAddress();

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.dstAccelerationStructure = handle_;
    buildInfo.scratchData.deviceAddress = scratch_->deviceAddress();

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = count;
    const VkAccelerationStructureBuildRangeInfoKHR* rangeInfos[] = {&rangeInfo};

    pfnCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, rangeInfos);
}
