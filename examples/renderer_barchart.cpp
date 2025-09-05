#include "renderer_barchart.h"
#include <stdexcept>
#include <array>
#include <vector>
#include <fstream>
#include "src/ext/vk_initializers.h"

#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult err__ = (x); if (err__ != VK_SUCCESS) { throw std::runtime_error(std::string("Vulkan error ") + std::to_string(err__)); } } while(0)
#endif

namespace {

// 简单读取 .spv
std::vector<char> read_file(const char* path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error(std::string("Failed to open shader file: ") + path);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    file.close();
    return buf;
}

VkShaderModule create_shader_module(VkDevice device, const std::vector<char>& code)
{
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod{};
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &mod));
    return mod;
}

} // namespace

// ==== IRenderer 接口实现 ====

void BarChartRenderer::initialize(const RenderContext& ctx)
{
    create_pipelines(ctx);
    create_descriptors(ctx);
}

void BarChartRenderer::destroy(const RenderContext& ctx)
{
    destroy_descriptors(ctx.device);
    destroy_pipelines(ctx.device);
}

void BarChartRenderer::on_swapchain_resized(const RenderContext& ctx)
{
    // 重新写 dset 指向新的 offscreen view
    destroy_descriptors(ctx.device);
    create_descriptors(ctx);
}

void BarChartRenderer::record(VkCommandBuffer cmd, uint32_t width, uint32_t height, const RenderContext& ctx)
{
    // 1) offscreen 改为 GENERAL 供 compute 写
    transition_image(cmd,
                     ctx.offscreenImage,
                     VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_2_NONE,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     0,
                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    // 2) 绑定 compute 与描述符
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipes_.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipes_.layout,
                            0, 1, &dset_, 0, nullptr);

    // 3) push 常量
    struct Push {
        uint32_t W, H;
        float margin_px;
        float gap_px;
        float base_line_px;
        float max_value;
    } push;
    push.W = width;
    push.H = height;
    push.margin_px = params_.margin_px;
    push.gap_px = params_.gap_px;
    push.base_line_px = params_.base_line_px;
    push.max_value = params_.max_value;
    vkCmdPushConstants(cmd, pipes_.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);

    // 4) dispatch
    const uint32_t groupSizeX = 16;
    const uint32_t groupSizeY = 16;
    uint32_t gx = (width  + groupSizeX - 1) / groupSizeX;
    uint32_t gy = (height + groupSizeY - 1) / groupSizeY;
    vkCmdDispatch(cmd, gx, gy, 1);

    // 5) 准备拷贝
    transition_image(cmd,
                     ctx.offscreenImage,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);

    // 注意：engine 的 end_frame 在 present 前不会再改布局
    // ui_->render_overlay 需要 swapchain 在 TRANSFER_DST_OPTIMAL
    // 所以这里把 swapchain image 变为 TRANSFER_DST_OPTIMAL 并保持
    transition_image(cmd,
                     ctx.swapchainImage,
                     VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_NONE,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                     0,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT);

    // 6) blit 到 swapchain，拉伸铺满
    copy_offscreen_to_swapchain(cmd, ctx.offscreenImage, ctx.swapchainImage, ctx.frameExtent);
}

// ==== 内部资源 ====

void BarChartRenderer::create_pipelines(const RenderContext& ctx)
{
    // descriptor set layout: binding 0 = storage image
    VkDescriptorSetLayoutBinding b0{};
    b0.binding = 0;
    b0.descriptorCount = 1;
    b0.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1;
    dslci.pBindings = &b0;
    VK_CHECK(vkCreateDescriptorSetLayout(ctx.device, &dslci, nullptr, &pipes_.dsl));

    // pipeline layout
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(uint32_t) * 2 + sizeof(float) * 4;

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &pipes_.dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipes_.layout));

    // shader
    auto code = read_file("shaders/barchart.comp.spv");
    pipes_.cs = create_shader_module(ctx.device, code);

    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage = VkPipelineShaderStageCreateInfo{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        0,
        VK_SHADER_STAGE_COMPUTE_BIT,
        pipes_.cs,
        "main",
        nullptr
    };
    cpci.layout = pipes_.layout;

    VK_CHECK(vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipes_.pipeline));
}

void BarChartRenderer::create_descriptors(const RenderContext& ctx)
{
    // 分配 dset
    dset_ = ctx.descriptorAllocator->allocate(ctx.device, pipes_.dsl);

    // 写 binding 0 指向 offscreen image view，layout GENERAL
    VkDescriptorImageInfo ii{};
    ii.imageView = ctx.offscreenImageView;
    ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = dset_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &ii;
    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
}

void BarChartRenderer::destroy_pipelines(VkDevice device)
{
    if (pipes_.pipeline) { vkDestroyPipeline(device, pipes_.pipeline, nullptr); pipes_.pipeline = VK_NULL_HANDLE; }
    if (pipes_.cs) { vkDestroyShaderModule(device, pipes_.cs, nullptr); pipes_.cs = VK_NULL_HANDLE; }
    if (pipes_.layout) { vkDestroyPipelineLayout(device, pipes_.layout, nullptr); pipes_.layout = VK_NULL_HANDLE; }
    if (pipes_.dsl) { vkDestroyDescriptorSetLayout(device, pipes_.dsl, nullptr); pipes_.dsl = VK_NULL_HANDLE; }
}

void BarChartRenderer::destroy_descriptors(VkDevice device)
{
    // dset 由自定义 DescriptorAllocator 统一回收，无需在此显式销毁
    dset_ = VK_NULL_HANDLE;
}

// ==== 工具函数 ====

void BarChartRenderer::transition_image(VkCommandBuffer cmd,
                                        VkImage image,
                                        VkImageLayout oldLayout,
                                        VkImageLayout newLayout,
                                        VkPipelineStageFlags2 srcStage,
                                        VkPipelineStageFlags2 dstStage,
                                        VkAccessFlags2 srcAccess,
                                        VkAccessFlags2 dstAccess,
                                        VkImageAspectFlags aspect)
{
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.image = image;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.layerCount = 1;
    b.subresourceRange.levelCount = 1;

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void BarChartRenderer::copy_offscreen_to_swapchain(VkCommandBuffer cmd,
                                                   VkImage src,
                                                   VkImage dst,
                                                   VkExtent2D extent)
{
    VkImageBlit2 blit{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};

    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};

    VkBlitImageInfo2 info{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
    info.srcImage = src;
    info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    info.dstImage = dst;
    info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    info.filter = VK_FILTER_LINEAR;
    info.regionCount = 1;
    info.pRegions = &blit;

    vkCmdBlitImage2(cmd, &info);
}
