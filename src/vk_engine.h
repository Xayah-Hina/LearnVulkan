#ifndef VK_ENGINE_H
#define VK_ENGINE_H

#include "vk_descriptors.h"
#include "vk_mem_alloc.h"
#include <vulkan/vulkan.h>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <deque>
#include <functional>

constexpr unsigned int FRAME_OVERLAP = 2;

struct DeletionQueue
{
    std::deque<std::function<void()>> deleters;

    void push_function(std::function<void()>&& function)
    {
        deleters.push_back(function);
    }

    void flush()
    {
        // reverse iterate the deletion queue to execute all the functions
        for (auto it = deleters.rbegin(); it != deleters.rend(); it++)
        {
            (*it)(); //call functors
        }

        deleters.clear();
    }
};

struct AllocatedImage
{
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct FrameData
{
    VkSemaphore swapchainSemaphore, renderSemaphore;
    VkFence renderFence;

    VkCommandPool commandPool;
    VkCommandBuffer mainCommandBuffer;

    DeletionQueue deletionQueue;
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
    VulkanEngine();
    ~VulkanEngine();

public:
    void init();
    void cleanup();
    void run();

protected:
    struct
    {
        std::string name = "Vulkan Engine";
        int width = 1700;
        int height = 800;
        bool initialized{false};
        bool running{false};
        bool should_rendering{false};

        int frame_number{0};
        int current_background_effect{1};
    } STATE;

    void draw();

private:
    struct EngineContext
    {
        struct SDL_Window* window{nullptr};
        VkSurfaceKHR surface{};
        VkInstance instance{};
        VkDebugUtilsMessengerEXT debug_messenger{};
        VkPhysicalDevice physical{};
        VkDevice device{};
        VkQueue graphics_queue{};
        uint32_t graphics_queue_family{};
        VmaAllocator allocator{};
    };

    struct SwapchainSystem
    {
        VkSwapchainKHR swapchain{};
        VkFormat swapchain_image_format{};
        VkExtent2D swapchain_extent{};

        std::vector<VkImage> swapchain_images;
        std::vector<VkImageView> swapchain_image_views;

        AllocatedImage drawable_image;
    };

    FrameData& get_current_frame() { return frames[STATE.frame_number % FRAME_OVERLAP]; };
    FrameData frames[FRAME_OVERLAP];

    DescriptorAllocator globalDescriptorAllocator;
    VkDescriptorSet _drawImageDescriptors;
    VkDescriptorSetLayout _drawImageDescriptorLayout;

    VkPipeline _gradientPipeline;
    VkPipelineLayout _gradientPipelineLayout;
    std::vector<ComputeEffect> backgroundEffects;

    std::unique_ptr<EngineContext> ctx;
    std::unique_ptr<SwapchainSystem> swapchain;
    DeletionQueue mdq;
};


#endif //VK_ENGINE_H
