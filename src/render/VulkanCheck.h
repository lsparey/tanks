#pragma once

#include <stdexcept>
#include <string>

#include <vulkan/vulkan.h>

#define VK_CHECK(expr)                                                       \
    do {                                                                     \
        VkResult vkCheckResult_ = (expr);                                    \
        if (vkCheckResult_ != VK_SUCCESS) {                                  \
            throw std::runtime_error(std::string(#expr) +                    \
                                      " failed with VkResult " +              \
                                      std::to_string(vkCheckResult_));        \
        }                                                                     \
    } while (0)
