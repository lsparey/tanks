#pragma once

#include <array>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

// The single vertex format shared by every mesh in the prototype (terrain,
// boxes, shells, and the loaded tank model), so one pipeline suffices for
// the whole scene. uv defaults to (0,0), which is fine for meshes that don't
// sample a texture (they're bound to a plain white 1x1 default texture).
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv{0.0f};

    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> attrs{};
        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(Vertex, position);

        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(Vertex, normal);

        attrs[2].binding = 0;
        attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[2].offset = offsetof(Vertex, color);

        attrs[3].binding = 0;
        attrs[3].location = 3;
        attrs[3].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[3].offset = offsetof(Vertex, uv);
        return attrs;
    }
};
