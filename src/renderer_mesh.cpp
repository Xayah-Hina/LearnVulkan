#include "renderer_mesh.h"
#include "vk_initializers.h"
#include "vk_images.h"
#include "vk_pipelines.h"
#include <cstring>
#include <cmath>
#include <stdexcept>

#include "imgui.h"

#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult err__ = (x); if (err__ != VK_SUCCESS) throw std::runtime_error("VK err " + std::to_string(err__)); } while(0)
#endif

// ---- helpers ----
AllocatedBuffer MeshRenderer::create_buffer(VmaAllocator alloc, size_t size, VkBufferUsageFlags usage,
                                            VmaMemoryUsage memUsage, VmaAllocationCreateFlags flags)
{
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;

    VmaAllocationCreateInfo ai{};
    ai.usage = memUsage;
    ai.flags = flags; // 我们会用 VMA_ALLOCATION_CREATE_MAPPED_BIT

    AllocatedBuffer out{};
    VK_CHECK(vmaCreateBuffer(alloc, &bi, &ai, &out.buffer, &out.allocation, &out.info));
    return out;
}

void MeshRenderer::destroy_buffer(VmaAllocator alloc, const AllocatedBuffer& b)
{
    if (b.buffer) vmaDestroyBuffer(alloc, b.buffer, b.allocation);
}

void MeshRenderer::immediate_submit(VkDevice device, VkQueue queue, uint32_t qfamily,
                                    std::function<void(VkCommandBuffer)> &&fn)
{
    // 临时一次性命令池/缓冲/栅栏
    VkCommandPoolCreateInfo pci = vkinit::command_pool_create_info(qfamily, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    VkCommandPool pool{};
    VK_CHECK(vkCreateCommandPool(device, &pci, nullptr, &pool));

    VkCommandBufferAllocateInfo ai = vkinit::command_buffer_allocate_info(pool, 1);
    VkCommandBuffer cmd{};
    VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cmd));

    VkFenceCreateInfo fci = vkinit::fence_create_info();
    VkFence fence{};
    VK_CHECK(vkCreateFence(device, &fci, nullptr, &fence));

    VkCommandBufferBeginInfo bi = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    fn(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cbsi = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 si = vkinit::submit_info(&cbsi, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(queue, 1, &si, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(1'000'000'000)));

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
}

// ---- IRenderer ----
void MeshRenderer::initialize(const RenderContext& ctx)
{
    // 1) 创建图形管线（动态渲染）
    // pipeline layout：带 push constant（worldMatrix + deviceAddress）
    VkPushConstantRange pc{};
    pc.offset = 0;
    pc.size   = sizeof(GPUDrawPushConstants);
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo plci = vkinit::pipeline_layout_create_info();
    plci.pPushConstantRanges = &pc;
    plci.pushConstantRangeCount = 1;
    VK_CHECK(vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipelineLayout_));

    // 加载 shader
    VkShaderModule vs{}, fs{};
    if (!vkutil::load_shader_module("./shaders/colored_triangle_mesh.vert.spv", ctx.device, &vs))
        throw std::runtime_error("load VS failed");
    if (!vkutil::load_shader_module("./shaders/colored_triangle.frag.spv", ctx.device, &fs))
        throw std::runtime_error("load FS failed");

    PipelineBuilder pb;
    pb._pipelineLayout = pipelineLayout_;
    pb.set_shaders(vs, fs);
    pb.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pb.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pb.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pb.set_multisampling_none();
    pb.disable_blending();
    pb.disable_depthtest(); // 先不做深度，等你后续接“Depth”步骤

    // 目标颜色格式用 offscreen（与你 Engine 一致）
    pb.set_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT);
    pb.set_depth_format(VK_FORMAT_UNDEFINED);

    pipeline_ = pb.build_pipeline(ctx.device);

    vkDestroyShaderModule(ctx.device, vs, nullptr);
    vkDestroyShaderModule(ctx.device, fs, nullptr);

    // 2) 创建 mesh：一个矩形（两个三角形）
    struct Vertex { float px, py, pz; float r,g,b,a; };
    Vertex verts[4] = {
        {  0.5f,-0.5f,0,  0,0,0,1 },
        {  0.5f, 0.5f,0,  0.5f,0.5f,0.5f,1 },
        { -0.5f,-0.5f,0,  1,0,0,1 },
        { -0.5f, 0.5f,0,  0,1,0,1 }
    };
    uint32_t indices[6] = {0,1,2, 2,1,3};
    indexCount_ = 6;

    const VkDevice device = ctx.device;
    VmaAllocator allocator = ctx.allocator;

    const size_t vbSize = sizeof(verts);
    const size_t ibSize = sizeof(indices);

    // GPU-only 目标缓冲
    vertexBuffer_ = create_buffer(allocator, vbSize,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VMA_MEMORY_USAGE_GPU_ONLY);

    indexBuffer_  = create_buffer(allocator, ibSize,
                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VMA_MEMORY_USAGE_GPU_ONLY);

    // 设备地址
    VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addrInfo.buffer = vertexBuffer_.buffer;
    vertexDeviceAddress_ = vkGetBufferDeviceAddress(device, &addrInfo);

    // staging（CPU 可见 + 持久映射）
    AllocatedBuffer staging =
        create_buffer(allocator, vbSize + ibSize,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VMA_MEMORY_USAGE_CPU_ONLY,
                      VMA_ALLOCATION_CREATE_MAPPED_BIT);

    // 拷贝到 staging
    std::memcpy(static_cast<char*>(staging.info.pMappedData) + 0,        verts,   vbSize);
    std::memcpy(static_cast<char*>(staging.info.pMappedData) + vbSize,   indices, ibSize);

    // 立即提交，copy 到 GPU-only
    immediate_submit(device, /*queue*/ctx.graphics_queue, /*family*/ctx.graphics_queue_family, [&](VkCommandBuffer cmd){
        VkBufferCopy c0{0, 0, vbSize};
        vkCmdCopyBuffer(cmd, staging.buffer, vertexBuffer_.buffer, 1, &c0);

        VkBufferCopy c1{vbSize, 0, ibSize};
        vkCmdCopyBuffer(cmd, staging.buffer, indexBuffer_.buffer, 1, &c1);
    });

    // 销毁 staging
    destroy_buffer(allocator, staging);
}

void MeshRenderer::record(VkCommandBuffer cmd, uint32_t width, uint32_t height, const RenderContext& ctx)
{
    // 约定：和你的 Engine 对齐——先把 offscreen 当作渲染目标，
    // 再 copy 到 swapchain，最后**让 swapchain 保持在 TRANSFER_DST_OPTIMAL**
    // 以便 ImGuiLayer 接管并转 PRESENT。

    // offscreen: UNDEFINED/GENERAL -> COLOR_ATTACHMENT_OPTIMAL（清屏）
    vkutil::transition_image(cmd, ctx.offscreenImage,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkClearValue clear{};
    clear.color = {0.05f, 0.05f, 0.08f, 1.0f};

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView   = ctx.offscreenImageView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue  = clear;

    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.offset = {0,0};
    ri.renderArea.extent = {width, height};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;

    vkCmdBeginRendering(cmd, &ri);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    // 动态 viewport/scissor
    VkViewport vp{};
    vp.width  = static_cast<float>(width);
    vp.height = static_cast<float>(height);
    vp.minDepth = 0.f; vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D sc{{0,0},{width,height}};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // push constants
    GPUDrawPushConstants pc{};
    pc.worldMatrix = glm::mat4(1.0f);
    pc.vertexBuffer = vertexDeviceAddress_;
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pc);

    // 绑定索引缓冲（顶点数据在 shader 里通过设备地址取用）
    vkCmdBindIndexBuffer(cmd, indexBuffer_.buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);

    vkCmdEndRendering(cmd);

    // offscreen -> TRANSFER_SRC_OPTIMAL
    vkutil::transition_image(cmd, ctx.offscreenImage,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // swapchain: UNDEFINED -> TRANSFER_DST_OPTIMAL
    vkutil::transition_image(cmd, ctx.swapchainImage,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // copy
    vkutil::copy_image_to_image(cmd,
                                ctx.offscreenImage,
                                ctx.swapchainImage,
                                VkExtent2D{width, height},
                                VkExtent2D{width, height});

    // 注意：这里**不要**转 PRESENT，保持在 TRANSFER_DST_OPTIMAL，让 ImGuiLayer 接着画并最终转 PRESENT
}

void MeshRenderer::destroy(const RenderContext& ctx)
{
    if (pipeline_)        vkDestroyPipeline(ctx.device, pipeline_, nullptr);
    if (pipelineLayout_)  vkDestroyPipelineLayout(ctx.device, pipelineLayout_, nullptr);

    destroy_buffer(ctx.allocator, vertexBuffer_);
    destroy_buffer(ctx.allocator, indexBuffer_);

    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
}

void MeshRenderer::on_swapchain_resized(const RenderContext& ctx)
{
}

void MeshRenderer::on_imgui()
{
    if (ImGui::Begin("Mesh Renderer")) {
        ImGui::Text("Draws a rectangle via graphics pipeline");
        ImGui::End();
    }
}
