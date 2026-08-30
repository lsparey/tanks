#pragma once

#include <array>

#include <glm/glm.hpp>

#include "Buffer.h"
#include "CommandContext.h"
#include "Texture.h"
#include "VulkanContext.h"

// Owns the single graphics pipeline used for everything in the scene
// (terrain, boxes, shells, tank), plus the per-frame uniform buffer/
// descriptor set (set 0) it's bound to. Per-object data goes through a push
// constant (the model matrix + an unlit flag); per-frame data (view/proj/
// light) goes through the set-0 UBO, updated once per frame via
// updateFrameUBO. Set 1 is two combined-image-sampler bindings for whatever
// textures a given draw call wants (grass+rock for terrain, blended by
// world-space height via the heightBlend push constant; a plain white 1x1
// texture in both slots for everything else so their vertex colors are
// unaffected) -- allocated per-texture-pair via allocateMaterialDescriptorSet.
// Set 2 is a single acceleration-structure binding (the scene TLAS), read
// by basic.frag via VK_KHR_ray_query for shadow rays. Its handle changes
// every frame (SceneAccelerationStructure rebuilds in place per
// frame-in-flight slot), so there's one set 2 per frame-in-flight,
// rewritten each frame via updateTLASDescriptor.
// Set 3 is a combined-image-sampler for the *other* frame-in-flight slot's
// HistoryBuffer image (last frame's temporally-accumulated shadow term),
// read by basic.frag for reprojected blending. The pipeline itself also
// writes a second color attachment (this frame's blended result, for next
// frame's history) alongside the usual color output -- see historyFormat.
class Pipeline {
public:
    struct FrameUBO {
        glm::mat4 view;
        glm::mat4 proj;
        glm::mat4 prevViewProj;
        glm::vec4 lightDir;
        glm::vec4 cameraPos;
        glm::vec4 prevCameraPos;  // for basic.frag's depth-based disocclusion rejection
    };

    struct PushConstants {
        glm::mat4 model;
        float unlit = 0.0f;             // nonzero: skip lighting, draw fragColor at full brightness
        float specularStrength = 0.0f;  // 0: matte, higher: shinier/more metallic highlight
        // nonzero: blend between the material set's two textures by world-space
        // height (low points -> the second texture) instead of just sampling
        // the first -- see basic.frag. Only terrain sets this.
        float heightBlend = 0.0f;
        // Multiplies the material texture's own alpha to get the final
        // output alpha -- 1.0 (opaque) for everything except fading ground
        // decals like TrackMark. The main color attachment blends normally
        // (SRC_ALPHA/ONE_MINUS_SRC_ALPHA), which is a no-op for anything
        // that stays fully opaque, so this doesn't affect existing draws.
        float opacity = 1.0f;
    };

    Pipeline(VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat, VkFormat historyFormat);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void updateFrameUBO(const FrameUBO& ubo);
    // secondary is only sampled when a draw's PushConstants::heightBlend is
    // nonzero (terrain); everything else can pass the same texture for both
    // and ignore it.
    VkDescriptorSet allocateMaterialDescriptorSet(const Texture& primary, const Texture& secondary);
    void updateTLASDescriptor(size_t frameIndex, VkAccelerationStructureKHR tlas);
    void updateHistoryDescriptor(size_t frameIndex, VkImageView historyView, VkSampler historySampler);

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return pipelineLayout_; }
    VkDescriptorSet descriptorSet() const { return descriptorSet_; }
    VkDescriptorSet tlasDescriptorSet(size_t frameIndex) const { return tlasDescriptorSets_[frameIndex]; }
    VkDescriptorSet historyDescriptorSet(size_t frameIndex) const {
        return historyDescriptorSets_[frameIndex];
    }

private:
    void createDescriptorSetLayout();
    void createMaterialSetLayout();
    void createTLASSetLayout();
    void createHistorySetLayout();
    void createDescriptorPoolAndSet();
    void createPipelineLayout();
    void createPipeline(VkFormat colorFormat, VkFormat depthFormat, VkFormat historyFormat);
    VkShaderModule loadShaderModule(const char* relativePath);

    VulkanContext& ctx_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout tlasSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout historySetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    Buffer uniformBuffer_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, CommandContext::kFramesInFlight> tlasDescriptorSets_{};
    std::array<VkDescriptorSet, CommandContext::kFramesInFlight> historyDescriptorSets_{};
};
