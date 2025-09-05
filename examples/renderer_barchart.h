#ifndef RENDERER_BARCHART_H
#define RENDERER_BARCHART_H

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include "src/renderer_iface.h"
#include "src/ext/vk_descriptors.h"

class BarChartRenderer final : public IRenderer
{
public:
    BarChartRenderer() = default;
    ~BarChartRenderer() override = default;

    void initialize(const RenderContext& ctx) override;
    void destroy(const RenderContext& ctx) override;

    void record(VkCommandBuffer cmd, uint32_t width, uint32_t height, const RenderContext& ctx) override;
    void on_swapchain_resized(const RenderContext& ctx) override;
    void on_imgui() override {}  // 本例不需要 ImGui

private:
    struct Pipelines {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        VkShaderModule cs = VK_NULL_HANDLE;
    } pipes_;

    VkDescriptorSet dset_ = VK_NULL_HANDLE;

    // 简单参数，后续你可以暴露到 UI
    struct Params {
        float margin_px = 40.0f;    // 画布四周留白
        float gap_px = 14.0f;       // 柱子之间的间隙
        float base_line_px = 40.0f; // 底部保留作为坐标轴厚度
        float max_value = 10.0f;    // 归一化高度上限
    } params_;

    void create_pipelines(const RenderContext& ctx);
    void create_descriptors(const RenderContext& ctx);
    void destroy_pipelines(VkDevice device);
    void destroy_descriptors(VkDevice device);

    // 工具函数：同步与布局转换
    void transition_image(VkCommandBuffer cmd,
                          VkImage image,
                          VkImageLayout oldLayout,
                          VkImageLayout newLayout,
                          VkPipelineStageFlags2 srcStage,
                          VkPipelineStageFlags2 dstStage,
                          VkAccessFlags2 srcAccess,
                          VkAccessFlags2 dstAccess,
                          VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

    // 把 offscreen 拷贝到 swapchain（等比例拉伸至整个窗口）
    void copy_offscreen_to_swapchain(VkCommandBuffer cmd,
                                     VkImage src,
                                     VkImage dst,
                                     VkExtent2D extent);
};

#endif //RENDERER_BARCHART_H