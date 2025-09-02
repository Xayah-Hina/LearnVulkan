#ifndef RENDERER_IFACE_H
#define RENDERER_IFACE_H

#include "vk_mem_alloc.h"

#include <vulkan/vulkan.h>
#include <cstdint>

class DescriptorAllocator; // forward decl from your project

struct RenderContext
{
    VkDevice device{};
    VmaAllocator allocator{};

    VkExtent2D frameExtent{};
    VkFormat swapchainFormat{};

    // Provided by engine per frame
    VkImage swapchainImage{};

    // Engine-managed offscreen target that content can use
    VkImage offscreenImage{};
    VkImageView offscreenImageView{};

    // Global descriptor allocator owned by engine
    DescriptorAllocator* descriptorAllocator{};
};

class IRenderer
{
public:
    virtual ~IRenderer() = default;

    // Create content-specific GPU objects
    virtual void initialize(const RenderContext& ctx) = 0;

    // Record commands that produce the final presentable image
    // This function is called once per frame
    virtual void record(VkCommandBuffer cmd,
                        uint32_t width,
                        uint32_t height,
                        const RenderContext& ctx) = 0;

    // Destroy content-specific GPU objects
    virtual void destroy(const RenderContext& ctx) = 0;

    // Optional swapchain resize hook. Default no op.
    virtual void on_swapchain_resized(const RenderContext& ctx) {}

    // Optional ImGui hook. Default no op.
    virtual void on_imgui() {}
};


#endif //RENDERER_IFACE_H
