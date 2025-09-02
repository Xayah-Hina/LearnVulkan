#ifndef VK_ENGINE_H
#define VK_ENGINE_H


#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "vk_descriptors.h"
#include "vk_mem_alloc.h"

#include "renderer_iface.h"

struct DeletionQueue {
    std::vector<std::function<void()>> deleters;
    void push_function(std::function<void()>&& fn) { deleters.emplace_back(std::move(fn)); }
    void flush() {
        for (auto it = deleters.rbegin(); it != deleters.rend(); ++it) { (*it)(); }
        deleters.clear();
    }
};

struct AllocatedImage {
    VkImage image{};
    VkImageView imageView{};
    VmaAllocation allocation{};
    VkExtent3D imageExtent{};
    VkFormat imageFormat{};
};

constexpr unsigned int FRAME_OVERLAP = 2;

struct FrameData {
    VkSemaphore swapchainSemaphore{};
    VkSemaphore renderSemaphore{};
    VkFence renderFence{};
    VkCommandPool commandPool{};
    VkCommandBuffer mainCommandBuffer{};
    DeletionQueue deletionQueue;
};

class VulkanEngine {
public:
    VulkanEngine();
    ~VulkanEngine();

    void init();
    void run();
    void cleanup();

    // Allow plugging different renderers
    void set_renderer(std::unique_ptr<IRenderer> r) { renderer_ = std::move(r); }

private:
    // Frame helpers
    void begin_frame(uint32_t& imageIndex, VkCommandBuffer& cmd);
    void end_frame(uint32_t imageIndex, VkCommandBuffer cmd);

private:
    struct {
        std::string name = "Vulkan Engine";
        int width  = 1700;
        int height = 800;
        bool initialized{false};
        bool running{false};
        bool should_rendering{false};
        int  frame_number{0};
    } STATE;

    struct EngineContext {
        SDL_Window* window{nullptr};
        VkSurfaceKHR surface{};
        VkInstance instance{};
        VkDebugUtilsMessengerEXT debug_messenger{};
        VkPhysicalDevice physical{};
        VkDevice device{};
        VkQueue graphics_queue{};
        uint32_t graphics_queue_family{};
        VmaAllocator allocator{};
    } ctx_;

    struct SwapchainSystem {
        VkSwapchainKHR swapchain{};
        VkFormat swapchain_image_format{};
        VkExtent2D swapchain_extent{};
        std::vector<VkImage> swapchain_images;
        std::vector<VkImageView> swapchain_image_views;
        // Engine-offered offscreen target for content
        AllocatedImage drawable_image;
    } swapchain_;

    FrameData frames_[FRAME_OVERLAP];

    DescriptorAllocator globalDescriptorAllocator_;

    std::unique_ptr<IRenderer> renderer_;

    DeletionQueue mdq_;
};


#endif // VK_ENGINE_H
