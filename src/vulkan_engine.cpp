#include "vulkan_engine.h"
#include "vulkan_initializers.h"
#include "VkBootstrap.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

void VulkanEngine::init()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error("SDL_Init failed" + std::string(SDL_GetError()));
    if (!SDL_Vulkan_LoadLibrary(nullptr))
        throw std::runtime_error("SDL_Vulkan_LoadLibrary failed" + std::string(SDL_GetError()));
    if (!(_window = SDL_CreateWindow("Vulkan Engine", _width, _height,SDL_WINDOW_VULKAN)))
        throw std::runtime_error("SDL_CreateWindow failed" + std::string(SDL_GetError()));

    init_vulkan();
    init_swapchain();
    init_commands();
    init_sync_structures();

    _is_initialized = true;
}

void VulkanEngine::draw()
{
}

void VulkanEngine::run()
{
}

void VulkanEngine::cleanup()
{
    if (_is_initialized)
    {
        vkDeviceWaitIdle(_device);

        for (int i = 0; i < FRAME_OVERLAP; i++)
        {
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
            vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);
        }

        destroy_swapchain();
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_device, nullptr);
        vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
        vkDestroyInstance(_instance, nullptr);
        SDL_DestroyWindow(_window);
    }
}

void VulkanEngine::init_vulkan()
{
    vkb::InstanceBuilder builder;
    auto inst_ret = builder
                    .set_app_name("Example Vulkan Application")
                    .request_validation_layers(false)
                    .use_default_debug_messenger()
                    .require_api_version(1, 3, 0)
                    .build();

    vkb::Instance vkb_inst = inst_ret.value();
    _instance = vkb_inst.instance;
    _debug_messenger = vkb_inst.debug_messenger;

    if (!SDL_Vulkan_CreateSurface(_window, _instance, nullptr, &_surface))
        throw std::runtime_error("SDL_Vulkan_CreateSurface failed" + std::string(SDL_GetError()));

    VkPhysicalDeviceVulkan13Features features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features.dynamicRendering = true;
    features.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    vkb::PhysicalDeviceSelector selector{vkb_inst};
    vkb::PhysicalDevice physicalDevice = selector
                                         .set_minimum_version(1, 3)
                                         .set_required_features_13(features)
                                         .set_required_features_12(features12)
                                         .set_surface(_surface)
                                         .select()
                                         .value();
    vkb::DeviceBuilder deviceBuilder{physicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();
    _device = vkbDevice.device;
    _chosenGPU = physicalDevice.physical_device;

    _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

void VulkanEngine::init_swapchain()
{
    create_swapchain(_width, _height);
}

void VulkanEngine::init_commands()
{
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool);

        // allocate the default command buffer that we will use for rendering
        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

        vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer);
    }
}

void VulkanEngine::init_sync_structures()
{
    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence);
        vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore);
        vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore);
    }
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{_chosenGPU, _device, _surface};
    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkSurfaceFormatKHR desiredFormat{};
    desiredFormat.format = _swapchainImageFormat;
    desiredFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    vkb::Swapchain vkbSwapchain = swapchainBuilder
                                  .set_desired_format(desiredFormat)
                                  .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                  .set_desired_extent(width, height)
                                  .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                                  .build()
                                  .value();
    _swapchainExtent = vkbSwapchain.extent;
    _swapchain = vkbSwapchain.swapchain;
    _swapchainImages = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanEngine::destroy_swapchain()
{
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    for (int i = 0; i < _swapchainImageViews.size(); i++)
        vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
}
