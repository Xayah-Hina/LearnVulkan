#ifndef VK_ENGINE_H
#define VK_ENGINE_H

#include <vulkan/vulkan.h>
#include <vector>

constexpr unsigned int FRAME_OVERLAP = 2;

struct FrameData
{
    VkSemaphore _swapchainSemaphore, _renderSemaphore;
    VkFence _renderFence;

    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;
};

class VulkanEngine
{
public:
    void init();
    void run();
    void draw();
    void cleanup();

protected:
    struct SDL_Window* _window{nullptr};
    VkExtent2D _windowExtent{1700u, 900u};

    VkInstance _instance;
    VkPhysicalDevice _chosenGPU;
    VkDevice _device;
    VkSurfaceKHR _surface;
    VkDebugUtilsMessengerEXT _debug_messenger;

    VkSwapchainKHR _swapchain;
    VkFormat _swapchainImageFormat;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    VkExtent2D _swapchainExtent;

    FrameData& get_current_frame() { return _frames[STATE._frameNumber % FRAME_OVERLAP]; };
    FrameData _frames[FRAME_OVERLAP];
    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;

    struct
    {
        int _frameNumber = 0;
        bool initialized = false;
    } STATE;

private:
    void _init_sdl_window();
    void _init_vulkan();
    void _init_swapchain();
    void _init_commands();
    void _init_sync_structures();
};


#endif //VK_ENGINE_H
