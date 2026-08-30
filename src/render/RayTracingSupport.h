#pragma once

#include <vulkan/vulkan.h>

// Shared feature-struct chain for VK_KHR_acceleration_structure +
// VK_KHR_ray_query + buffer device address (a Vulkan 1.2 feature, not part
// of VkPhysicalDeviceVulkan13Features, but required for AS builds: vertex/
// index buffers used as build inputs need a queryable device address).
// Used both to query device support (VulkanContext's device suitability
// check) and to enable the features at logical device creation, so the two
// don't duplicate/drift from each other.
struct RayTracingFeatureChain {
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{};

    RayTracingFeatureChain() {
        bufferDeviceAddress.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        accelerationStructure.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        rayQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        bufferDeviceAddress.pNext = &accelerationStructure;
        accelerationStructure.pNext = &rayQuery;
    }
};

inline bool queryRayTracingSupport(VkPhysicalDevice device) {
    RayTracingFeatureChain chain;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &chain.bufferDeviceAddress;

    vkGetPhysicalDeviceFeatures2(device, &features2);
    return chain.bufferDeviceAddress.bufferDeviceAddress &&
           chain.accelerationStructure.accelerationStructure && chain.rayQuery.rayQuery;
}
