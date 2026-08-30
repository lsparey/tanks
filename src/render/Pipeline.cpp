#include "Pipeline.h"

#include <fstream>
#include <stdexcept>
#include <vector>

#include "Vertex.h"
#include "VulkanCheck.h"

namespace {

std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + path);
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

}  // namespace

Pipeline::Pipeline(VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat,
                   VkFormat historyFormat)
    : ctx_(ctx),
      uniformBuffer_(ctx, sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
    createDescriptorSetLayout();
    createMaterialSetLayout();
    createTLASSetLayout();
    createHistorySetLayout();
    createDescriptorPoolAndSet();
    createPipelineLayout();
    createPipeline(colorFormat, depthFormat, historyFormat);
}

Pipeline::~Pipeline() {
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(ctx_.device(), pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(ctx_.device(), pipelineLayout_, nullptr);
    if (descriptorPool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(ctx_.device(), descriptorPool_, nullptr);
    if (historySetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device(), historySetLayout_, nullptr);
    if (tlasSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device(), tlasSetLayout_, nullptr);
    if (materialSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device(), materialSetLayout_, nullptr);
    if (descriptorSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device(), descriptorSetLayout_, nullptr);
}

void Pipeline::updateFrameUBO(const FrameUBO& ubo) { uniformBuffer_.copyData(&ubo, sizeof(ubo)); }

void Pipeline::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    VK_CHECK(
        vkCreateDescriptorSetLayout(ctx_.device(), &layoutInfo, nullptr, &descriptorSetLayout_));
}

void Pipeline::createMaterialSetLayout() {
    // Bindings 1-3 are only actually sampled by basic.frag when a draw's
    // heightBlend push constant is nonzero (terrain); every other draw
    // still needs a valid descriptor bound at each (Vulkan requires it even
    // if the shader branch skips reading it), so callers just bind the same
    // texture into all four.
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(ctx_.device(), &layoutInfo, nullptr, &materialSetLayout_));
}

void Pipeline::createTLASSetLayout() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    VK_CHECK(vkCreateDescriptorSetLayout(ctx_.device(), &layoutInfo, nullptr, &tlasSetLayout_));
}

void Pipeline::createHistorySetLayout() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    VK_CHECK(vkCreateDescriptorSetLayout(ctx_.device(), &layoutInfo, nullptr, &historySetLayout_));
}

void Pipeline::createDescriptorPoolAndSet() {
    // 8 in active use as of this comment (terrain, track, cloud, rock,
    // bark, leaf, crate, white) -- headroom kept above that for future
    // material types.
    constexpr uint32_t kMaxMaterialSets = 12;
    constexpr uint32_t kTLASSets = CommandContext::kFramesInFlight;
    constexpr uint32_t kHistorySets = CommandContext::kFramesInFlight;

    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // Each material set now has 4 bindings (two high-terrain variants, two
    // low-terrain variants -- see createMaterialSetLayout).
    poolSizes[1].descriptorCount = kMaxMaterialSets * 4 + kHistorySets;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[2].descriptorCount = kTLASSets;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1 + kMaxMaterialSets + kTLASSets + kHistorySets;

    VK_CHECK(vkCreateDescriptorPool(ctx_.device(), &poolInfo, nullptr, &descriptorPool_));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;

    VK_CHECK(vkAllocateDescriptorSets(ctx_.device(), &allocInfo, &descriptorSet_));

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer_.handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(FrameUBO);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(ctx_.device(), 1, &write, 0, nullptr);

    // TLAS sets are allocated now but not written -- the TLAS doesn't exist
    // yet at Pipeline construction time. Application writes the current
    // frame's TLAS handle into its slot every frame via updateTLASDescriptor.
    for (auto& set : tlasDescriptorSets_) {
        VkDescriptorSetAllocateInfo tlasAllocInfo{};
        tlasAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        tlasAllocInfo.descriptorPool = descriptorPool_;
        tlasAllocInfo.descriptorSetCount = 1;
        tlasAllocInfo.pSetLayouts = &tlasSetLayout_;
        VK_CHECK(vkAllocateDescriptorSets(ctx_.device(), &tlasAllocInfo, &set));
    }

    // Same story: allocated now, written later once HistoryBuffer exists
    // (and rewritten whenever it's recreated on resize).
    for (auto& set : historyDescriptorSets_) {
        VkDescriptorSetAllocateInfo historyAllocInfo{};
        historyAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        historyAllocInfo.descriptorPool = descriptorPool_;
        historyAllocInfo.descriptorSetCount = 1;
        historyAllocInfo.pSetLayouts = &historySetLayout_;
        VK_CHECK(vkAllocateDescriptorSets(ctx_.device(), &historyAllocInfo, &set));
    }
}

void Pipeline::updateTLASDescriptor(size_t frameIndex, VkAccelerationStructureKHR tlas) {
    VkWriteDescriptorSetAccelerationStructureKHR asWrite{};
    asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures = &tlas;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = &asWrite;
    write.dstSet = tlasDescriptorSets_[frameIndex];
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    write.descriptorCount = 1;

    vkUpdateDescriptorSets(ctx_.device(), 1, &write, 0, nullptr);
}

void Pipeline::updateHistoryDescriptor(size_t frameIndex, VkImageView historyView,
                                        VkSampler historySampler) {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = historyView;
    imageInfo.sampler = historySampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = historyDescriptorSets_[frameIndex];
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(ctx_.device(), 1, &write, 0, nullptr);
}

VkDescriptorSet Pipeline::allocateMaterialDescriptorSet(const Texture& highA, const Texture& highB,
                                                          const Texture& lowA, const Texture& lowB) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &materialSetLayout_;

    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(ctx_.device(), &allocInfo, &set));

    const Texture* textures[4] = {&highA, &highB, &lowA, &lowB};
    VkDescriptorImageInfo imageInfos[4]{};
    VkWriteDescriptorSet writes[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[i].imageView = textures[i]->view();
        imageInfos[i].sampler = textures[i]->sampler();

        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(ctx_.device(), 4, writes, 0, nullptr);
    return set;
}

void Pipeline::createPipelineLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkDescriptorSetLayout setLayouts[] = {descriptorSetLayout_, materialSetLayout_, tlasSetLayout_,
                                           historySetLayout_};

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 4;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(ctx_.device(), &layoutInfo, nullptr, &pipelineLayout_));
}

VkShaderModule Pipeline::loadShaderModule(const char* relativePath) {
    std::string path = std::string(ASSET_ROOT) + "/shaders/" + relativePath;
    std::vector<char> code = readFile(path);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(ctx_.device(), &createInfo, nullptr, &module));
    return module;
}

void Pipeline::createPipeline(VkFormat colorFormat, VkFormat depthFormat, VkFormat historyFormat) {
    VkShaderModule vertModule = loadShaderModule("basic.vert.spv");
    VkShaderModule fragModule = loadShaderModule("basic.frag.spv");

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    auto bindingDescription = Vertex::bindingDescription();
    auto attributeDescriptions = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDescription;
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributeDescriptions.size());
    vertexInput.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    // Front face is CCW as seen from outside a mesh; combined with the
    // negative-viewport-height trick used at draw time to correct Vulkan's
    // flipped-Y NDC without needing to also flip winding here.
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = ctx_.msaaSamples();

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // Standard alpha blending -- a no-op for every draw that outputs alpha=1
    // (everything except fading ground decals like TrackMark, see
    // PushConstants::opacity), so this doesn't change how existing opaque
    // geometry looks: result = src*1 + dst*0 = src, same as no blending.
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    // Second color attachment: this frame's blended shadow value (R), AO
    // value (G), and a view-distance proxy (B, for next frame's
    // disocclusion check), written alongside the final lit color so next
    // frame's temporal-accumulation read has something fresh to sample.
    VkPipelineColorBlendAttachmentState historyBlendAttachment{};
    historyBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    historyBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[] = {colorBlendAttachment,
                                                                    historyBlendAttachment};

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorBlendAttachments;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkFormat colorAttachmentFormats[] = {colorFormat, historyFormat};
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 2;
    renderingInfo.pColorAttachmentFormats = colorAttachmentFormats;
    renderingInfo.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;

    VK_CHECK(vkCreateGraphicsPipelines(ctx_.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                        &pipeline_));

    vkDestroyShaderModule(ctx_.device(), vertModule, nullptr);
    vkDestroyShaderModule(ctx_.device(), fragModule, nullptr);
}
