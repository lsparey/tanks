#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "../scene/DynamicLight.h"
#include "Buffer.h"
#include "CommandContext.h"
#include "Texture.h"
#include "VulkanContext.h"

// Owns the single graphics pipeline used for everything in the scene
// (terrain, boxes, shells, tank), plus the per-frame uniform buffer/
// descriptor set (set 0) it's bound to. Per-object data goes through a push
// constant (including the model matrix/material flags); repeated static
// geometry instead indexes a transform SSBO in set 0. Per-frame data
// (view/proj/light) shares set 0 and is updated once per frame. Set 1 is
// four combined-image-sampler bindings for
// whatever textures a given draw call wants: for terrain, a "high" pair
// (two grass variants, patch-blended by a noise mask) and a "low" pair (two
// gravel variants, likewise), with the high/low pair itself then blended by
// world-space height via the heightBlend push constant -- see basic.frag.
// Everything else just binds the same plain white 1x1 texture into all four
// slots so their vertex colors are unaffected -- allocated via
// allocateMaterialDescriptorSet.
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
    static constexpr uint32_t kMaxRasterInstances = 8192;

    struct FrameUBO {
        glm::mat4 view;
        glm::mat4 proj;
        glm::mat4 prevViewProj;
        glm::vec4 lightDir;
        glm::vec4 cameraPos;
        glm::vec4 prevCameraPos;  // for basic.frag's depth-based disocclusion rejection
        // Muzzle-flash/explosion point lights -- see DynamicLight.h. xyz is
        // world position, w is the falloff radius (0 means "inactive slot,
        // skip" -- see basic.frag). rgb is color, w is peak intensity.
        // Application::drawFrame fills up to kMaxDynamicLights of these
        // from dynamicLights_ each frame and zeroes the rest.
        std::array<glm::vec4, kMaxDynamicLights> dynamicLightPosRadius{};
        std::array<glm::vec4, kMaxDynamicLights> dynamicLightColorIntensity{};
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
        // How strongly the RT-reflection term (see traceReflection in
        // basic.frag) contributes to the final color -- separate from
        // specularStrength (which drives the Blinn-Phong highlight/Fresnel
        // "shininess" look) so a surface can be, say, only mildly shiny but
        // strongly reflective (water) or the reverse, without the two
        // effects being tied to one shared knob.
        float reflectivity = 0.0f;
        // Perturbs the shading normal used by specular/Fresnel/reflection
        // (not the real geometric normal used for diffuse/shadow rays)
        // with an animated ripple pattern -- water only; 0 elsewhere.
        float waveStrength = 0.0f;
        // Perturbs the *diffuse* normal (see basic.frag's litNormal) using
        // the material texture's own luminance as a fake heightfield, via
        // the true per-instance tangent (fragTangent) -- gives a flat decal
        // some readable surface depth under lighting instead of looking
        // like a printed-on sticker. Track marks only; 0 elsewhere. (Terrain
        // gets its own version of this gated by heightBlend instead, since
        // it can take a cheaper world-axis-aligned shortcut track marks
        // can't -- see basic.frag.)
        float bumpStrength = 0.0f;
        // Nonzero for a moving rigid body (currently just the tank) --
        // basic.frag's temporal shadow/AO history reprojection assumes a
        // fragment's world position was where it is now back in the
        // previous frame too, which is only true for static geometry; a
        // moving object needs this flagged so history is skipped for it
        // instead of ghosting/smearing. Kept as its own field rather than
        // inferred from specularStrength (which used to double as this
        // flag) so the two can vary independently -- see Application's tank
        // draw loop, which now gives the tank's camo (painted) and metal
        // parts different specularStrength values.
        float isDynamicObject = 0.0f;
        // Material-specific shading without another descriptor set. Values
        // must match basic.frag: 0 generic, 1 terrain, 2 foliage, 3 rock.
        float materialType = 0.0f;
        // Nonzero selects instanceTransforms[gl_InstanceIndex] in basic.vert
        // instead of model, allowing repeated static meshes to batch.
        float isInstanced = 0.0f;
    };

    Pipeline(VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat, VkFormat historyFormat);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void updateFrameUBO(const FrameUBO& ubo);
    void updateInstanceTransforms(const std::vector<glm::mat4>& transforms);
    // highA/highB and lowA/lowB are only sampled/blended when a draw's
    // PushConstants::heightBlend is nonzero (terrain). terrainControl is an
    // optional fifth terrain-only lookup; other sets fall back to highA.
    VkDescriptorSet allocateMaterialDescriptorSet(const Texture& highA, const Texture& highB,
                                                   const Texture& lowA, const Texture& lowB,
                                                   const Texture* terrainControl = nullptr);
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
    Buffer instanceBuffer_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, CommandContext::kFramesInFlight> tlasDescriptorSets_{};
    std::array<VkDescriptorSet, CommandContext::kFramesInFlight> historyDescriptorSets_{};
};
