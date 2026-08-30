#pragma once

#include <vulkan/vulkan.h>

// Unlike VK_KHR_swapchain, acceleration-structure/ray-query functions aren't
// statically exported by the Vulkan loader -- they must be resolved via
// vkGetDeviceProcAddr, same as the debug-utils messenger functions in
// VulkanContext.cpp. Call loadRayTracingFunctions once, right after logical
// device creation, before anything in AccelerationStructure runs.
extern PFN_vkGetAccelerationStructureBuildSizesKHR pfnGetAccelerationStructureBuildSizesKHR;
extern PFN_vkCreateAccelerationStructureKHR pfnCreateAccelerationStructureKHR;
extern PFN_vkDestroyAccelerationStructureKHR pfnDestroyAccelerationStructureKHR;
extern PFN_vkCmdBuildAccelerationStructuresKHR pfnCmdBuildAccelerationStructuresKHR;
extern PFN_vkGetAccelerationStructureDeviceAddressKHR pfnGetAccelerationStructureDeviceAddressKHR;

void loadRayTracingFunctions(VkDevice device);
