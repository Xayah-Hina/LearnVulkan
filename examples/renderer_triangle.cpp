#include "renderer_triangle.h"

#include "src/ext/vk_initializers.h"
#include "src/ext/vk_images.h"
#include "src/ext/vk_pipelines.h"
#include <stdexcept>

#ifndef VK_CHECK
#define VK_CHECK(x) do{ VkResult _e=(x); if(_e!=VK_SUCCESS) throw std::runtime_error("Vulkan error "+std::to_string(_e)); }while(0)
#endif

void TriangleRenderer::initialize(const RenderContext& ctx)
{
    // 1) 空的 pipeline layout（无 descriptor / 无 push 常量）
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VK_CHECK(vkCreatePipelineLayout(ctx.device, &pli, nullptr, &pipelineLayout_));

    // 2) 加载着色器（使用 vkguide 的生成三角形着色器）
    VkShaderModule vs{}, fs{};
    if (!vkutil::load_shader_module("./shaders/colored_triangle.vert.spv", ctx.device, &vs))
        throw std::runtime_error("failed to load colored_triangle.vert.spv");
    if (!vkutil::load_shader_module("./shaders/colored_triangle.frag.spv", ctx.device, &fs)) {
        vkDestroyShaderModule(ctx.device, vs, nullptr);
        throw std::runtime_error("failed to load colored_triangle.frag.spv");
    }

    // 3) 用 PipelineBuilder 生成图形管线（Dynamic Rendering）
    //    目标颜色格式 = offscreen 的格式：R16G16B16A16_SFLOAT（由 Engine 创建）
    {
        PipelineBuilder pb;
        pb._pipelineLayout = pipelineLayout_;
        pb.set_shaders(vs, fs);
        pb.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.set_polygon_mode(VK_POLYGON_MODE_FILL);
        pb.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        pb.set_multisampling_none();
        pb.disable_blending();
        pb.disable_depthtest(); // 目前不启用深度

        pb.set_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT);
        pb.set_depth_format(VK_FORMAT_UNDEFINED);

        pipeline_ = pb.build_pipeline(ctx.device);
    }

    vkDestroyShaderModule(ctx.device, vs, nullptr);
    vkDestroyShaderModule(ctx.device, fs, nullptr);
}

void TriangleRenderer::record(VkCommandBuffer cmd,
                              uint32_t width, uint32_t height,
                              const RenderContext& ctx)
{
    // --- A. 把 offscreen 切到 ColorAttachment ---
    vkutil::transition_image(cmd,
                             ctx.offscreenImage,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // --- B. Dynamic Rendering 画三角形到 offscreen ---
    VkClearValue clear{};
    clear.color = { { 0.05f, 0.05f, 0.08f, 1.0f } };

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView   = ctx.offscreenImageView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue  = clear;

    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.offset = {0, 0};
    ri.renderArea.extent = {width, height};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;

    vkCmdBeginRendering(cmd, &ri);

    // 绑定管线 + 动态视口/裁剪
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport vp{};
    vp.x = 0; vp.y = 0;
    vp.width  = static_cast<float>(width);
    vp.height = static_cast<float>(height);
    vp.minDepth = 0.f; vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = {width, height};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // 无顶点缓冲：VS 用 gl_VertexIndex 生成三角形
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);

    // --- C. 拷到 swapchain ---
    vkutil::transition_image(cmd,
                             ctx.offscreenImage,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    vkutil::transition_image(cmd,
                             ctx.swapchainImage,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkutil::copy_image_to_image(cmd,
                                ctx.offscreenImage,
                                ctx.swapchainImage,
                                VkExtent2D{width, height},
                                VkExtent2D{width, height});

    // 注意：保持 swapchain 在 TRANSFER_DST_OPTIMAL，
    // 你的 ImGuiLayer 会基于此转到 COLOR_ATTACHMENT 再绘制，并最终转 PRESENT。
}

void TriangleRenderer::destroy(const RenderContext& ctx)
{
    if (pipeline_) {
        vkDestroyPipeline(ctx.device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_) {
        vkDestroyPipelineLayout(ctx.device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
}

void TriangleRenderer::on_swapchain_resized(const RenderContext& ctx)
{
}

void TriangleRenderer::on_imgui()
{
}
