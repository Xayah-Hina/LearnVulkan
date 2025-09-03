#ifndef RENDERER_MESH_H
#define RENDERER_MESH_H

#include "renderer_iface.h"
#include <glm/mat4x4.hpp>
#include <functional>

struct GPUDrawPushConstants {
    glm::mat4 worldMatrix;     // 64 bytes
    VkDeviceAddress vertexBuffer; // 8 bytes (设备地址)
};

struct AllocatedBuffer {
    VkBuffer buffer{};
    VmaAllocation allocation{};
    VmaAllocationInfo info{};  // 持久映射时存放 pMappedData
};

class MeshRenderer final : public IRenderer {
public:
    void initialize(const RenderContext& ctx) override;
    void record(VkCommandBuffer cmd, uint32_t w, uint32_t h, const RenderContext& ctx) override;
    void destroy(const RenderContext& ctx) override;
    void on_swapchain_resized(const RenderContext& ctx) override;

    // （可选）UI 调试
    void on_imgui() override;

private:
    // 上传 mesh 用
    AllocatedBuffer create_buffer(VmaAllocator alloc, size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memUsage, VmaAllocationCreateFlags flags = 0);
    void destroy_buffer(VmaAllocator alloc, const AllocatedBuffer& b);

    // 一次性提交（staging copy）
    void immediate_submit(VkDevice device, VkQueue queue, uint32_t qfamily,
                          std::function<void(VkCommandBuffer)> &&fn);

    // 资源
    VkPipelineLayout pipelineLayout_{};
    VkPipeline pipeline_{};

    // mesh buffers
    AllocatedBuffer vertexBuffer_;   // GPU-only + device address
    AllocatedBuffer indexBuffer_;    // GPU-only
    uint32_t indexCount_{6};
    VkDeviceAddress vertexDeviceAddress_{};

    // 上传临时对象的销毁延后到 destroy()
};

#endif //RENDERER_MESH_H
