#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Buffer.h"
#include "VulkanContext.h"

// Minimal screen-space overlay: solid-color NDC quads (crosshair, boxes-
// remaining ticks), no lighting, no depth test -- always drawn on top.
// Geometry is rebuilt and re-uploaded every frame since the HUD's content
// (box count) changes; the vertex count is tiny, so a fresh host-visible
// upload per frame is simpler than any kind of instancing/caching.
class HudRenderer {
public:
    struct Vertex {
        glm::vec2 position;
        glm::vec3 color;
    };

    HudRenderer(VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat);
    ~HudRenderer();

    HudRenderer(const HudRenderer&) = delete;
    HudRenderer& operator=(const HudRenderer&) = delete;

    void begin();
    // halfSize is in NDC units; pass an already aspect-corrected x so
    // squares drawn this way look square on screen regardless of window
    // aspect ratio (see Application, which knows the current aspect).
    void addQuad(glm::vec2 centerNDC, glm::vec2 halfSizeNDC, glm::vec3 color);
    void render(VkCommandBuffer cmd);

private:
    VulkanContext& ctx_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    Buffer vertexBuffer_;
    std::vector<Vertex> pending_;
};
