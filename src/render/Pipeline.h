#pragma once

#include <glm/glm.hpp>

#include "Buffer.h"
#include "Texture.h"
#include "VulkanContext.h"

// Owns the single graphics pipeline used for everything in the scene
// (terrain, boxes, shells, tank), plus the per-frame uniform buffer/
// descriptor set (set 0) it's bound to. Per-object data goes through a push
// constant (the model matrix + an unlit flag); per-frame data (view/proj/
// light) goes through the set-0 UBO, updated once per frame via
// updateFrameUBO. Set 1 is a single combined-image-sampler binding for
// whatever texture a given draw call wants (grass for terrain, a plain
// white 1x1 texture for everything else so their vertex colors are
// unaffected) -- allocated per-texture via allocateMaterialDescriptorSet.
class Pipeline {
public:
    struct FrameUBO {
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 lightDir;
        glm::vec4 cameraPos;
    };

    struct PushConstants {
        glm::mat4 model;
        float unlit = 0.0f;             // nonzero: skip lighting, draw fragColor at full brightness
        float specularStrength = 0.0f;  // 0: matte, higher: shinier/more metallic highlight
    };

    Pipeline(VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void updateFrameUBO(const FrameUBO& ubo);
    VkDescriptorSet allocateMaterialDescriptorSet(const Texture& texture);

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return pipelineLayout_; }
    VkDescriptorSet descriptorSet() const { return descriptorSet_; }

private:
    void createDescriptorSetLayout();
    void createMaterialSetLayout();
    void createDescriptorPoolAndSet();
    void createPipelineLayout();
    void createPipeline(VkFormat colorFormat, VkFormat depthFormat);
    VkShaderModule loadShaderModule(const char* relativePath);

    VulkanContext& ctx_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    Buffer uniformBuffer_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
};
