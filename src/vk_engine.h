#ifndef VK_ENGINE_H
#define VK_ENGINE_H


#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "ext/vk_descriptors.h"
#include "vk_mem_alloc.h"

#include "renderer_iface.h"
#include "imgui_layer.h"

struct DeletionQueue
{
    std::vector<std::function<void()>> deleters;
    void push_function(std::function<void()>&& fn) { deleters.emplace_back(std::move(fn)); }

    void flush()
    {
        for (auto it = deleters.rbegin(); it != deleters.rend(); ++it) { (*it)(); }
        deleters.clear();
    }
};


constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine
{
public: // Main Functions
    void init();
    void run();
    void cleanup();
    void set_renderer(std::unique_ptr<IRenderer> r) { renderer_ = std::move(r); }

public: // Engine State
    struct
    {
        std::string name = "Vulkan Engine";
        int width = 1700;
        int height = 800;
        bool initialized{false};
        bool running{false};
        bool should_rendering{false};
        int frame_number{0};
        bool resize_requested{false};
    } state_;

public: // Constructors and Operators
    VulkanEngine() = default;
    ~VulkanEngine() = default;
    VulkanEngine(const VulkanEngine&) = delete;
    VulkanEngine& operator=(const VulkanEngine&) = delete;
    VulkanEngine(VulkanEngine&&) noexcept = default;
    VulkanEngine& operator=(VulkanEngine&&) noexcept = default;

private: // Engine Context
    void create_context(int window_width, int window_height, const char* app_name);
    void destroy_context();

    struct EngineContext
    {
        VkInstance instance{};
        VkDebugUtilsMessengerEXT debug_messenger{};
        SDL_Window* window{nullptr};
        VkSurfaceKHR surface{};
        VkPhysicalDevice physical{};
        VkDevice device{};
        VkQueue graphics_queue{};
        uint32_t graphics_queue_family{};
        VmaAllocator allocator{};
        DescriptorAllocator descriptor_allocator;
    } ctx_;

private: // Swapchain and Offscreen Drawable
    void create_swapchain(uint32_t width, uint32_t height);
    void destroy_swapchain();
    void recreate_swapchain();
    void create_offscreen_drawable(uint32_t width, uint32_t height);
    void destroy_offscreen_drawable();

    struct AllocatedImage
    {
        VkImage image{};
        VkImageView imageView{};
        VmaAllocation allocation{};
        VkExtent3D imageExtent{};
        VkFormat imageFormat{};
    };

    struct SwapchainSystem
    {
        VkSwapchainKHR swapchain{};
        VkFormat swapchain_image_format{};
        VkExtent2D swapchain_extent{};
        std::vector<VkImage> swapchain_images;
        std::vector<VkImageView> swapchain_image_views;
        // Engine-offered offscreen target for content
        AllocatedImage drawable_image;
        AllocatedImage depth_image{};
    } swapchain_;

private: // Frame Rendering
    void create_command_buffers();
    void destroy_command_buffers();
    void begin_frame(uint32_t& imageIndex, VkCommandBuffer& cmd);
    void end_frame(uint32_t imageIndex, VkCommandBuffer cmd);

    struct FrameData
    {
        VkSemaphore swapchainSemaphore{};
        VkSemaphore renderSemaphore{};
        VkFence renderFence{};
        VkCommandPool commandPool{};
        VkCommandBuffer mainCommandBuffer{};
        DeletionQueue deletionQueue;
    } frames_[FRAME_OVERLAP];

private: // Renderer
    void create_renderer();
    void destroy_renderer();
    std::unique_ptr<IRenderer> renderer_;

private: // ImGui
    void create_imgui();
    void destroy_imgui();
    std::unique_ptr<ImGuiLayer> ui_;
    DeletionQueue mdq_;
};


#endif // VK_ENGINE_H
