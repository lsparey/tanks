#include "VulkanContext.h"

#include <cstring>
#include <iostream>
#include <optional>
#include <set>
#include <vector>

#include "RayTracingFunctions.h"
#include "RayTracingSupport.h"
#include "VulkanCheck.h"

namespace {

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_QUERY_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* /*userData*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[validation] " << callbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& info) {
    info = {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
}

VkResult createDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    const VkAllocationCallbacks* allocator, VkDebugUtilsMessengerEXT* messenger) {
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!func) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return func(instance, createInfo, allocator, messenger);
}

void destroyDebugUtilsMessengerEXT(VkInstance instance,
                                    VkDebugUtilsMessengerEXT messenger,
                                    const VkAllocationCallbacks* allocator) {
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func) func(instance, messenger, allocator);
}

bool checkValidationLayerSupport() {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> available(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, available.data());

    for (const char* wanted : kValidationLayers) {
        bool found = false;
        for (const auto& layer : available) {
            if (std::strcmp(wanted, layer.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

std::vector<const char*> getRequiredInstanceExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions,
                                         glfwExtensions + glfwExtensionCount);
    if (kEnableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
        if (indices.isComplete()) break;
    }
    return indices;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

    std::set<std::string> required(kDeviceExtensions.begin(), kDeviceExtensions.end());
    for (const auto& ext : available) {
        required.erase(ext.extensionName);
    }
    return required.empty();
}

bool supportsVulkan13Features(VkPhysicalDevice device) {
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;

    vkGetPhysicalDeviceFeatures2(device, &features2);
    return features13.dynamicRendering && features13.synchronization2;
}

int scoreDevice(VkPhysicalDevice device, VkSurfaceKHR surface) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    if (props.apiVersion < VK_API_VERSION_1_3) return -1;
    if (!checkDeviceExtensionSupport(device)) return -1;
    if (!supportsVulkan13Features(device)) return -1;
    // Ray tracing (via VK_KHR_ray_query, not a full ray-tracing pipeline) is
    // a hard requirement for this project going forward -- there's no
    // raster-only fallback path, by design.
    if (!queryRayTracingSupport(device)) return -1;
    if (!findQueueFamilies(device, surface).isComplete()) return -1;

    uint32_t formatCount = 0, presentModeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (formatCount == 0 || presentModeCount == 0) return -1;

    switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 300;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 200;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 100;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return 10;
        default: return 0;
    }
}

}  // namespace

VulkanContext::VulkanContext(GLFWwindow* window) : window_(window) {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
}

VulkanContext::~VulkanContext() {
    if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
    if (debugMessenger_ != VK_NULL_HANDLE) {
        destroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
}

void VulkanContext::createInstance() {
    if (kEnableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error(
            "validation layers requested but VK_LAYER_KHRONOS_validation is not available");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "luke-game";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    auto extensions = getRequiredInstanceExtensions();

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (kEnableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        createInfo.ppEnabledLayerNames = kValidationLayers.data();
        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance_));
}

void VulkanContext::setupDebugMessenger() {
    if (!kEnableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);
    VK_CHECK(createDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_));
}

void VulkanContext::createSurface() {
    VK_CHECK(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_));
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("no Vulkan-capable physical devices found");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;
    for (auto device : devices) {
        int score = scoreDevice(device, surface_);
        if (score > bestScore) {
            bestScore = score;
            best = device;
        }
    }

    if (best == VK_NULL_HANDLE || bestScore < 0) {
        throw std::runtime_error(
            "no suitable device found: need Vulkan 1.3 (dynamic rendering + "
            "synchronization2) with VK_KHR_ray_query + VK_KHR_acceleration_structure support");
    }

    physicalDevice_ = best;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    std::cout << "Selected GPU: " << props.deviceName << std::endl;
    std::cout << "Ray query support: " << (queryRayTracingSupport(physicalDevice_) ? "yes" : "no")
               << std::endl;

    // 4x MSAA if the device supports it for both color and depth at once,
    // otherwise 2x, otherwise none -- 4x is the usual sweet spot between
    // visibly smoothing triangle edges (the tank's now-faceted hull panels
    // in particular) and cost; this hobby project has no need to reach for
    // 8x.
    VkSampleCountFlags sampleCounts =
        props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    if (sampleCounts & VK_SAMPLE_COUNT_4_BIT) {
        msaaSamples_ = VK_SAMPLE_COUNT_4_BIT;
    } else if (sampleCounts & VK_SAMPLE_COUNT_2_BIT) {
        msaaSamples_ = VK_SAMPLE_COUNT_2_BIT;
    } else {
        msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    }
    std::cout << "MSAA samples: " << msaaSamples_ << std::endl;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice_, surface_);
    graphicsQueueFamily_ = indices.graphicsFamily.value();
    presentQueueFamily_ = indices.presentFamily.value();
}

void VulkanContext::createLogicalDevice() {
    std::set<uint32_t> uniqueQueueFamilies = {graphicsQueueFamily_, presentQueueFamily_};
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;
    for (uint32_t family : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = family;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    RayTracingFeatureChain rtFeatures;
    rtFeatures.bufferDeviceAddress.bufferDeviceAddress = VK_TRUE;
    rtFeatures.accelerationStructure.accelerationStructure = VK_TRUE;
    rtFeatures.rayQuery.rayQuery = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.pNext = &rtFeatures.bufferDeviceAddress;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;
    // Needed because Pipeline's two color attachments (final color + the
    // single-channel shadow-history output) don't share the same write
    // mask -- without this, only identical pAttachments entries are legal.
    features2.features.independentBlend = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features2;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();
    if (kEnableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        createInfo.ppEnabledLayerNames = kValidationLayers.data();
    }

    VK_CHECK(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_));

    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);

    loadRayTracingFunctions(device_);
}
