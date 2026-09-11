#pragma once

#include <vector>
#include <string_view>

#include <glm/glm.hpp>

#include "Buffer.h"
#include "HudGeometry.h"
#include "VulkanContext.h"

// Uploads the shared CPU HUD geometry as a single alpha-blended draw.
class HudRenderer : public HudGeometry {
public:
    HudRenderer(VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat);
    ~HudRenderer();

    HudRenderer(const HudRenderer&) = delete;
    HudRenderer& operator=(const HudRenderer&) = delete;

    void render(VkCommandBuffer cmd);

private:
    VulkanContext& ctx_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    Buffer vertexBuffer_;
};
