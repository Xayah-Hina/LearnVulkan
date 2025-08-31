#ifndef VULKAN_ENGINE_H
#define VULKAN_ENGINE_H

#include <vulkan/vulkan.h>
#include <vector>

struct FrameData {
    VkSemaphore _swapchainSemaphore, _renderSemaphore;
    VkFence _renderFence;

    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine
{
public:
    struct SDL_Window* _window{nullptr};
    bool _stop_rendering{false};
    bool _is_initialized{false};

public:
    void init();
    void draw();
    void run();
    void cleanup();

private:
    void init_vulkan();
    void init_swapchain();
    void init_commands();
    void init_sync_structures();

    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debug_messenger;
    VkPhysicalDevice _chosenGPU;
    VkDevice _device;
    VkSurfaceKHR _surface;

    void create_swapchain(uint32_t width, uint32_t height);
    void destroy_swapchain();
    VkSwapchainKHR _swapchain;
    VkFormat _swapchainImageFormat;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    VkExtent2D _swapchainExtent;

    uint32_t _width = 1700;
    uint32_t _height = 900;


    FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };
    FrameData _frames[FRAME_OVERLAP];
    int _frameNumber{0};
    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;
};


#endif //VULKAN_ENGINE_H
