#ifndef VK_ENGINE_H
#define VK_ENGINE_H

#include "vk_mem_alloc.h"
#include "vk_descriptors.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <deque>
#include <functional>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
constexpr unsigned int FRAME_OVERLAP = 2;

// ================================================================
struct AllocatedImage
{
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

// ===============================================================

struct DeletionQueue
{
    std::deque<std::function<void()>> deletors;

    void push_function(std::function<void()>&& function)
    {
        deletors.push_back(function);
    }

    void flush()
    {
        // reverse iterate the deletion queue to execute all the functions
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
        {
            (*it)(); //call functors
        }

        deletors.clear();
    }
};

struct FrameData
{
    VkSemaphore _swapchainSemaphore, _renderSemaphore;
    VkFence _renderFence;

    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;

    DeletionQueue _deletionQueue;
};

struct ComputePushConstants
{
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

struct ComputeEffect
{
    const char* name;

    VkPipeline pipeline;
    VkPipelineLayout layout;

    ComputePushConstants data;
};

class VulkanEngine
{
public:
    void init();
    void run();
    void draw();
    void cleanup();

protected:
    void draw_background(VkCommandBuffer cmd);
    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);

protected:
    struct SDL_Window* _window{nullptr};
    VkExtent2D _windowExtent{1700u, 900u};

    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debug_messenger;
    VkPhysicalDevice _chosenGPU;
    VkDevice _device;

    VkSurfaceKHR _surface;
    VkSwapchainKHR _swapchain;
    VkFormat _swapchainImageFormat;
    VkExtent2D _swapchainExtent;

    std::vector<VkFramebuffer> _framebuffers;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;

    FrameData& get_current_frame() { return _frames[STATE._frameNumber % FRAME_OVERLAP]; };
    FrameData _frames[FRAME_OVERLAP];
    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;

    struct
    {
        int currentBackgroundEffect = 0;
        int _frameNumber = 0;
        bool initialized = false;
    } STATE;

    DeletionQueue _mainDeletionQueue;
    VmaAllocator _allocator;

    DescriptorAllocator globalDescriptorAllocator;
    VkDescriptorSet _drawImageDescriptors;
    VkDescriptorSetLayout _drawImageDescriptorLayout;

    VkPipeline _gradientPipeline;
    VkPipelineLayout _gradientPipelineLayout;

    AllocatedImage _drawImage;
    VkExtent2D _drawExtent;

    // immediate submit structures
    VkFence _immFence;
    VkCommandBuffer _immCommandBuffer;
    VkCommandPool _immCommandPool;

    std::vector<ComputeEffect> backgroundEffects;

private:
    void _init_sdl_window();
    void _init_vulkan();
    void _init_swapchain();
    void _init_commands();
    void _init_sync_structures();
    void _init_pipelines();
    void _init_descriptors();
    void _init_imgui();


    void create_swapchain();
    void rebuild_swapchain();
    void destroy_swapchain();
};


#endif //VK_ENGINE_H
