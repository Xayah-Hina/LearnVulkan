#ifndef RENDERER_BARCHART_FONT_H
#define RENDERER_BARCHART_FONT_H

#include <vulkan/vulkan.h>
#include "src/renderer_iface.h"
#include "src/ext/vk_descriptors.h"
#include "vk_mem_alloc.h"
#include <array>
#include <string>
#include <vector>

class BarChartRendererMSDF final : public IRenderer {
public:
    void initialize(const RenderContext& ctx) override;
    void destroy(const RenderContext& ctx) override;
    void record(VkCommandBuffer cmd, uint32_t width, uint32_t height, const RenderContext& ctx) override;
    void on_swapchain_resized(const RenderContext& ctx) override;
    void on_imgui() override {}

    // 供你设置图集路径（默认指向 CMake 生成物）
    void set_msdf_paths(const std::string& png, const std::string& json) {
        atlas_png_ = png; atlas_json_ = json;
    }

private:
    // —— 柱状图 compute（沿用你之前的实现） ——
    struct BarPipe {
        VkPipeline pipeline{};
        VkPipelineLayout layout{};
        VkDescriptorSetLayout dsl{};
        VkShaderModule cs{};
        VkDescriptorSet dset{};
    } bar_;

    // —— MSDF 文字 compute ——
    struct TextPipe {
        VkPipeline pipeline{};
        VkPipelineLayout layout{};
        VkDescriptorSetLayout dsl{};
        VkShaderModule cs{};
        VkDescriptorSet dset{};
    } text_;

    // offscreen storage image 已由引擎提供，bar_.dset 里绑定 binding0

    // 字体图集资源
    VkImage        atlas_image_{};
    VmaAllocation  atlas_alloc_{};
    VkImageView    atlas_view_{};
    VkSampler      atlas_sampler_{};
    uint32_t       atlas_w_{}, atlas_h_{};

    // glyph 实例 SSBO
    VkBuffer       glyph_buf_{};
    VmaAllocation  glyph_alloc_{};
    uint32_t       glyph_cap_ = 256; // 最多实例数

    // uv 表，仅做 0..9（你需要可以扩展）
    struct UvRect { float u0,v0,u1,v1; };
    UvRect uv_digits_[10]{};

    // 参数：和柱子计算一致
    struct Params {
        float margin_px = 40.0f;
        float gap_px = 14.0f;
        float base_line_px = 40.0f;
        float max_value = 10.0f;
        float label_px = 20.0f;
        float label_gap_px = 6.0f;
        float pxRange = 8.0f;  // 与生成图集一致
    } params_;

    // 路径
    std::string atlas_png_;
    std::string atlas_json_;

    // 内部函数
    void create_bar_pipeline(const RenderContext& ctx);
    void create_text_pipeline(const RenderContext& ctx);
    void create_bar_descriptors(const RenderContext& ctx);
    void create_text_descriptors(const RenderContext& ctx);

    void destroy_bar_pipeline(VkDevice d);
    void destroy_text_pipeline(VkDevice d);
    void destroy_font_resources(VkDevice d, VmaAllocator a);
    void destroy_glyph_ssbo(VkDevice d, VmaAllocator a);

    void load_msdf_atlas(const RenderContext& ctx);          // 读 PNG + 上传到 GPU
    void parse_msdf_json();                                   // 从 JSON 读出 0..9 的 uv
    void ensure_glyph_ssbo(const RenderContext& ctx);         // 创建 SSBO

    // 每帧构建 glyph 实例数据并写入 SSBO
    void build_digits_for_bars(uint32_t W, uint32_t H, const RenderContext& ctx);

    // 小工具：过渡布局（同步2）
    void transition_image(VkCommandBuffer cmd, VkImage img,
                          VkImageLayout oldL, VkImageLayout newL,
                          VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
                          VkAccessFlags2 srcAcc, VkAccessFlags2 dstAcc);
    void copy_offscreen_to_swapchain(VkCommandBuffer cmd, VkImage src, VkImage dst, VkExtent2D extent);
};

#endif //RENDERER_BARCHART_FONT_H