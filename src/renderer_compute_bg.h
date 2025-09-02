#ifndef RENDERER_COMPUTE_BG_H
#define RENDERER_COMPUTE_BG_H


#include "renderer_iface.h"
#include <glm/vec4.hpp>
#include <vector>

struct ComputePushConstants {
    glm::vec4 data1{};
    glm::vec4 data2{};
    glm::vec4 data3{};
    glm::vec4 data4{};
};

struct ComputeEffect {
    const char* name{};
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    ComputePushConstants data{};
};

class ComputeBackgroundRenderer final : public IRenderer {
public:
    void initialize(const RenderContext& ctx) override;
    void record(VkCommandBuffer cmd,
                uint32_t width,
                uint32_t height,
                const RenderContext& ctx) override;
    void destroy(const RenderContext& ctx) override;

    void on_imgui() override;

    // Optional API to switch effect at runtime
    void set_effect_index(int idx) { current_effect_ = idx; }

private:
    // Descriptors
    VkDescriptorSetLayout drawImageSetLayout_{};
    VkDescriptorSet drawImageSet_{};

    // Pipelines
    VkPipelineLayout pipelineLayout_{};
    std::vector<ComputeEffect> effects_{};

    // Defered destructions are handled by the engine main deletion queue
    // For simplicity here we destroy in destroy()

    int current_effect_{1};
};



#endif //RENDERER_COMPUTE_BG_H