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
#include <vector>

// =======================================================
// Constants
// =======================================================
constexpr unsigned int FRAME_OVERLAP = 2;

// =======================================================
// Utility: Deletion Queue
// Collects cleanup callbacks and flushes them in reverse order
// =======================================================
struct DeletionQueue {
    std::deque<std::function<void()>> deleters;

    void push_function(std::function<void()>&& function) {
        deleters.push_back(std::move(function));
    }

    void flush() {
        for (auto it = deleters.rbegin(); it != deleters.rend(); ++it) {
            (*it)(); // call cleanup function
        }
        deleters.clear();
    }
};

// =======================================================
// GPU Resources
// =======================================================
struct AllocatedImage {
    VkImage image{};
    VkImageView imageView{};
    VmaAllocation allocation{};
    VkExtent3D imageExtent{};
    VkFormat imageFormat{};
};

// =======================================================
// Per-Frame Data
// Holds sync objects and command buffers
// =======================================================
struct FrameData {
    VkSemaphore swapchainSemaphore{};
    VkSemaphore renderSemaphore{};
    VkFence renderFence{};
    VkCommandPool commandPool{};
    VkCommandBuffer mainCommandBuffer{};
    DeletionQueue deletionQueue;
};

// =======================================================
// Compute Pipeline Resources
// =======================================================
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

// =======================================================
// Vulkan Engine
// =======================================================
class VulkanEngine {
public:
    VulkanEngine();
    ~VulkanEngine();

    void init();     // Initialize Vulkan and resources
    void cleanup();  // Destroy resources
    void run();      // Main loop

protected:
    void draw();     // Render a frame

private:
    // ----------------------------
    // Engine state (window + flags)
    // ----------------------------
    struct {
        std::string name = "Vulkan Engine";
        int width = 1700;
        int height = 800;
        bool initialized{false};
        bool running{false};
        bool should_rendering{false};
        int frame_number{0};
        int current_background_effect{1};
    } STATE;

    // ----------------------------
    // Vulkan Context
    // ----------------------------
    struct EngineContext {
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

    // ----------------------------
    // Swapchain
    // ----------------------------
    struct SwapchainSystem {
        VkSwapchainKHR swapchain{};
        VkFormat swapchain_image_format{};
        VkExtent2D swapchain_extent{};
        std::vector<VkImage> swapchain_images;
        std::vector<VkImageView> swapchain_image_views;
        AllocatedImage drawable_image; // offscreen render target
    };

    // ----------------------------
    // Helpers
    // ----------------------------
    FrameData& get_current_frame() {
        return frames[STATE.frame_number % FRAME_OVERLAP];
    }

    // ----------------------------
    // Members
    // ----------------------------
    FrameData frames[FRAME_OVERLAP];

    DescriptorAllocator globalDescriptorAllocator;
    VkDescriptorSet _drawImageDescriptors{};
    VkDescriptorSetLayout _drawImageDescriptorLayout{};

    VkPipeline _gradientPipeline{};
    VkPipelineLayout _gradientPipelineLayout{};

    std::vector<ComputeEffect> backgroundEffects;

    std::unique_ptr<EngineContext> ctx;
    std::unique_ptr<SwapchainSystem> swapchain;

    DeletionQueue mdq; // main deletion queue
};

#endif // VK_ENGINE_H
