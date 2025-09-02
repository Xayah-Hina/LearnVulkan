#include "renderer_compute_bg.h"
#include "renderer_compute_bg.h"

#include "vk_descriptors.h"
#include "vk_images.h"
#include "vk_initializers.h"
#include "vk_pipelines.h"

#include <stdexcept>
#include <cmath>
#include <algorithm>

#include "imgui.h"

#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult err__ = (x); if (err__ != VK_SUCCESS) { throw std::runtime_error(std::string("Vulkan error ") + std::to_string(err__)); } } while(0)
#endif

void ComputeBackgroundRenderer::initialize(const RenderContext& ctx)
{
    // Descriptor set layout for storage image at binding 0
    {
        DescriptorLayoutBuilder b;
        b.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        drawImageSetLayout_ = b.build(ctx.device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    // Allocate descriptor set from global pool
    drawImageSet_ = ctx.descriptorAllocator->allocate(ctx.device, drawImageSetLayout_);

    // Point it to engine-provided offscreen image view
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imgInfo.imageView = ctx.offscreenImageView;

    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = drawImageSet_;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);

    // Pipeline layout with push constants
    {
        VkPushConstantRange pc{};
        pc.offset = 0;
        pc.size = sizeof(ComputePushConstants);
        pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkPipelineLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        ci.setLayoutCount = 1;
        ci.pSetLayouts = &drawImageSetLayout_;
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &pc;

        VK_CHECK(vkCreatePipelineLayout(ctx.device, &ci, nullptr, &pipelineLayout_));
    }

    // Create two simple compute pipelines
    VkShaderModule gradientShader{};
    VkShaderModule skyShader{};

    if (!vkutil::load_shader_module("./shaders/gradient_color.comp.spv", ctx.device, &gradientShader))
    {
        throw std::runtime_error("Failed to load gradient shader");
    }
    if (!vkutil::load_shader_module("./shaders/sky.comp.spv", ctx.device, &skyShader))
    {
        throw std::runtime_error("Failed to load sky shader");
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.pName = "main";

    VkComputePipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pci.layout = pipelineLayout_;

    // gradient
    stage.module = gradientShader;
    pci.stage = stage;
    ComputeEffect gradient{};
    gradient.name = "gradient";
    gradient.layout = pipelineLayout_;
    gradient.data = {};
    gradient.data.data1 = glm::vec4(1.f, 0.f, 0.f, 1.f);
    gradient.data.data2 = glm::vec4(0.f, 0.f, 1.f, 1.f);
    VK_CHECK(vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &pci, nullptr, &gradient.pipeline));

    // sky
    stage.module = skyShader;
    pci.stage = stage;
    ComputeEffect sky{};
    sky.name = "sky";
    sky.layout = pipelineLayout_;
    sky.data = {};
    sky.data.data1 = glm::vec4(0.1f, 0.2f, 0.4f, 0.97f);
    VK_CHECK(vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &pci, nullptr, &sky.pipeline));

    effects_.push_back(gradient);
    effects_.push_back(sky);

    // Shader modules no longer needed
    vkDestroyShaderModule(ctx.device, gradientShader, nullptr);
    vkDestroyShaderModule(ctx.device, skyShader, nullptr);
}

void ComputeBackgroundRenderer::record(VkCommandBuffer cmd,
                                       uint32_t width,
                                       uint32_t height,
                                       const RenderContext& ctx)
{
    // Transition offscreen to GENERAL for compute write
    vkutil::transition_image(cmd, ctx.offscreenImage,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_GENERAL);

    // Bind compute effect and write to offscreen
    ComputeEffect& fx = effects_[std::clamp(current_effect_, 0, (int)effects_.size() - 1)];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fx.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout_, 0, 1, &drawImageSet_, 0, nullptr);
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(ComputePushConstants), &fx.data);

    const uint32_t gx = static_cast<uint32_t>(std::ceil(width / 16.0));
    const uint32_t gy = static_cast<uint32_t>(std::ceil(height / 16.0));
    vkCmdDispatch(cmd, gx, gy, 1);

    // Copy offscreen to current swapchain image
    vkutil::transition_image(cmd, ctx.offscreenImage,
                             VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(cmd, ctx.swapchainImage,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkutil::copy_image_to_image(cmd,
                                ctx.offscreenImage,
                                ctx.swapchainImage,
                                VkExtent2D{width, height},
                                VkExtent2D{width, height});

    vkutil::transition_image(cmd, ctx.swapchainImage,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

void ComputeBackgroundRenderer::destroy(const RenderContext& ctx)
{
    // Destroy pipelines
    for (auto& e : effects_)
    {
        if (e.pipeline)
        {
            vkDestroyPipeline(ctx.device, e.pipeline, nullptr);
            e.pipeline = VK_NULL_HANDLE;
        }
    }
    effects_.clear();

    // Destroy pipeline layout
    if (pipelineLayout_)
    {
        vkDestroyPipelineLayout(ctx.device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (drawImageSetLayout_)
    {
        vkDestroyDescriptorSetLayout(ctx.device, drawImageSetLayout_, nullptr);
        drawImageSetLayout_ = VK_NULL_HANDLE;
    }
}

void ComputeBackgroundRenderer::on_imgui()
{
    ImGui::Begin("Background");
    ImGui::Text("Effect index: %d", current_effect_);
    int e = current_effect_;
    if (ImGui::RadioButton("Gradient", e == 0)) e = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Sky", e == 1)) e = 1;
    if (e != current_effect_) current_effect_ = e;
    ImGui::End();
}
