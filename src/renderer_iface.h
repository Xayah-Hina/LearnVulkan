#ifndef RENDERER_IFACE_H
#define RENDERER_IFACE_H

#include "vk_mem_alloc.h"

#include <vulkan/vulkan.h>
#include <cstdint>

struct  DescriptorAllocator; // forward decl from your project

struct RenderContext
{
    // ========== EngineContext ==========
    VkDevice device{};
    VmaAllocator allocator{};
    DescriptorAllocator* descriptorAllocator{};
    VkQueue graphics_queue{};
    uint32_t graphics_queue_family{};

    // ========== Swapchain ==========
    VkExtent2D frameExtent{};
    VkFormat swapchainFormat{};
    // Provided by engine per frame
    VkImage swapchainImage{};
    // Engine-managed offscreen target that content can use
    VkImage offscreenImage{};
    VkImageView offscreenImageView{};
    // Engine-managed depth image for 3D rendering
    VkImage depthImage{VK_NULL_HANDLE};
    VkImageView depthImageView{VK_NULL_HANDLE};
};

class IRenderer
{
public:
    virtual ~IRenderer() = default;
    virtual void initialize(const RenderContext& ctx) = 0;
    virtual void record(VkCommandBuffer cmd, uint32_t width, uint32_t height, const RenderContext& ctx) = 0;
    virtual void destroy(const RenderContext& ctx) = 0;
    virtual void on_swapchain_resized(const RenderContext& ctx) = 0;
    virtual void on_imgui() = 0;
};


#endif //RENDERER_IFACE_H
