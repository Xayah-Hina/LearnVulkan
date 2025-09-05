#ifndef RENDERER_TRIANGLE_H
#define RENDERER_TRIANGLE_H

#include "src/renderer_iface.h"

class TriangleRenderer final : public IRenderer
{
public:
    void initialize(const RenderContext& ctx) override;
    void record(VkCommandBuffer cmd,
                uint32_t width, uint32_t height,
                const RenderContext& ctx) override;
    void destroy(const RenderContext& ctx) override;
    void on_swapchain_resized(const RenderContext& ctx) override;
    void on_imgui() override;

private:
    VkPipelineLayout pipelineLayout_{};
    VkPipeline pipeline_{};
};


#endif //RENDERER_TRIANGLE_H
