#include "vk_engine.h"

#include "vk_initializers.h"
#include "vk_images.h"
#include "VkBootstrap.h"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include <SDL3/SDL_vulkan.h>

#include <stdexcept>
#include <cmath>

#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult err__ = (x); if (err__ != VK_SUCCESS) { throw std::runtime_error(std::string("Vulkan error ") + std::to_string(err__)); } } while(0)
#endif

VulkanEngine::VulkanEngine() = default;
VulkanEngine::~VulkanEngine() = default;

void VulkanEngine::init()
{
    // Window
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    ctx_.window = SDL_CreateWindow("Vulkan Engine", STATE.width, STATE.height, SDL_WINDOW_VULKAN);
    if (!ctx_.window)
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

    // Instance
    vkb::InstanceBuilder ib;
    auto inst_ret = ib.set_app_name(STATE.name.c_str())
                      .request_validation_layers(false)
                      .use_default_debug_messenger()
                      .require_api_version(1, 3, 0)
                      .build();
    vkb::Instance vkb_inst = inst_ret.value();
    ctx_.instance = vkb_inst.instance;
    ctx_.debug_messenger = vkb_inst.debug_messenger;

    // Surface
    if (!SDL_Vulkan_CreateSurface(ctx_.window, ctx_.instance, nullptr, &ctx_.surface))
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());

    // Device and Queue
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.bufferDeviceAddress = VK_TRUE;
    f12.descriptorIndexing = VK_TRUE;

    vkb::PhysicalDeviceSelector sel{vkb_inst};
    vkb::PhysicalDevice phys = sel.set_surface(ctx_.surface)
                                  .set_minimum_version(1, 3)
                                  .set_required_features_13(f13)
                                  .set_required_features_12(f12)
                                  .select().value();
    ctx_.physical = phys.physical_device;

    vkb::DeviceBuilder db{phys};
    vkb::Device vkbDev = db.build().value();
    ctx_.device = vkbDev.device;
    ctx_.graphics_queue = vkbDev.get_queue(vkb::QueueType::graphics).value();
    ctx_.graphics_queue_family = vkbDev.get_queue_index(vkb::QueueType::graphics).value();

    // VMA
    {
        VmaAllocatorCreateInfo ac{};
        ac.physicalDevice = ctx_.physical;
        ac.device = ctx_.device;
        ac.instance = ctx_.instance;
        ac.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        VK_CHECK(vmaCreateAllocator(&ac, &ctx_.allocator));
        mdq_.push_function([&]() { vmaDestroyAllocator(ctx_.allocator); });
    }

    // Swapchain
    vkb::SwapchainBuilder sb{ctx_.physical, ctx_.device, ctx_.surface};
    swapchain_.swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;
    vkb::Swapchain sc = sb.set_desired_format(VkSurfaceFormatKHR{swapchain_.swapchain_image_format, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                          .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                          .set_desired_extent(STATE.width, STATE.height)
                          .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                          .build().value();
    swapchain_.swapchain = sc.swapchain;
    swapchain_.swapchain_extent = sc.extent;
    swapchain_.swapchain_images = sc.get_images().value();
    swapchain_.swapchain_image_views = sc.get_image_views().value();

    // Offscreen image offered to content
    swapchain_.drawable_image.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    swapchain_.drawable_image.imageExtent = {swapchain_.swapchain_extent.width, swapchain_.swapchain_extent.height, 1};

    VkImageUsageFlags offUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageCreateInfo imgci = vkinit::image_create_info(swapchain_.drawable_image.imageFormat, offUsage, swapchain_.drawable_image.imageExtent);

    VmaAllocationCreateInfo ainfo{};
    ainfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    ainfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VK_CHECK(vmaCreateImage(ctx_.allocator, &imgci, &ainfo, &swapchain_.drawable_image.image, &swapchain_.drawable_image.allocation, nullptr));

    VkImageViewCreateInfo viewci = vkinit::imageview_create_info(swapchain_.drawable_image.imageFormat, swapchain_.drawable_image.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(ctx_.device, &viewci, nullptr, &swapchain_.drawable_image.imageView));

    mdq_.push_function([&]()
    {
        vkDestroyImageView(ctx_.device, swapchain_.drawable_image.imageView, nullptr);
        vmaDestroyImage(ctx_.allocator, swapchain_.drawable_image.image, swapchain_.drawable_image.allocation);
    });

    // Per-frame resources
    VkCommandPoolCreateInfo poolci = vkinit::command_pool_create_info(ctx_.graphics_queue_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        VK_CHECK(vkCreateCommandPool(ctx_.device, &poolci, nullptr, &frames_[i].commandPool));
        VkCommandBufferAllocateInfo cbai = vkinit::command_buffer_allocate_info(frames_[i].commandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(ctx_.device, &cbai, &frames_[i].mainCommandBuffer));
    }
    VkFenceCreateInfo fci = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo sci = vkinit::semaphore_create_info();
    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        VK_CHECK(vkCreateFence(ctx_.device, &fci, nullptr, &frames_[i].renderFence));
        VK_CHECK(vkCreateSemaphore(ctx_.device, &sci, nullptr, &frames_[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(ctx_.device, &sci, nullptr, &frames_[i].renderSemaphore));
    }

    // Global descriptor allocator
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0f}};
    globalDescriptorAllocator_.init_pool(ctx_.device, 10, sizes);
    mdq_.push_function([&]() { globalDescriptorAllocator_.destroy_pool(ctx_.device); });

    // Create and initialize default renderer if not set from outside
    if (!renderer_)
    {
        extern std::unique_ptr<IRenderer> CreateDefaultComputeRenderer();
        renderer_ = CreateDefaultComputeRenderer();
    }

    RenderContext rctx{};
    rctx.device = ctx_.device;
    rctx.allocator = ctx_.allocator;
    rctx.frameExtent = swapchain_.swapchain_extent;
    rctx.swapchainFormat = swapchain_.swapchain_image_format;
    rctx.offscreenImage = swapchain_.drawable_image.image;
    rctx.offscreenImageView = swapchain_.drawable_image.imageView;
    rctx.descriptorAllocator = &globalDescriptorAllocator_;

    renderer_->initialize(rctx);

    STATE.initialized = true;
    STATE.running = true;
    STATE.should_rendering = true;
}

void VulkanEngine::begin_frame(uint32_t& imageIndex, VkCommandBuffer& cmd)
{
    FrameData& fr = frames_[STATE.frame_number % FRAME_OVERLAP];

    VK_CHECK(vkWaitForFences(ctx_.device, 1, &fr.renderFence, VK_TRUE, 1000000000));
    fr.deletionQueue.flush();

    VkResult acq = vkAcquireNextImageKHR(ctx_.device, swapchain_.swapchain, 1000000000, fr.swapchainSemaphore, nullptr, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR)
    {
        throw std::runtime_error("Swapchain recreation is not implemented in this sample");
    }
    VK_CHECK(acq);

    VK_CHECK(vkResetFences(ctx_.device, 1, &fr.renderFence));
    VK_CHECK(vkResetCommandBuffer(fr.mainCommandBuffer, 0));

    cmd = fr.mainCommandBuffer;
    VkCommandBufferBeginInfo bi = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
}

void VulkanEngine::end_frame(uint32_t imageIndex, VkCommandBuffer cmd)
{
    VK_CHECK(vkEndCommandBuffer(cmd));

    FrameData& fr = frames_[STATE.frame_number % FRAME_OVERLAP];

    VkCommandBufferSubmitInfo cbsi = vkinit::command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, fr.swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, fr.renderSemaphore);
    VkSubmitInfo2 si = vkinit::submit_info(&cbsi, &signalInfo, &waitInfo);

    VK_CHECK(vkQueueSubmit2(ctx_.graphics_queue, 1, &si, fr.renderFence));

    VkPresentInfoKHR pi = vkinit::present_info();
    pi.pSwapchains = &swapchain_.swapchain;
    pi.swapchainCount = 1;
    pi.pWaitSemaphores = &fr.renderSemaphore;
    pi.waitSemaphoreCount = 1;
    pi.pImageIndices = &imageIndex;

    VK_CHECK(vkQueuePresentKHR(ctx_.graphics_queue, &pi));
}

void VulkanEngine::run()
{
    SDL_Event e{};
    while (STATE.running)
    {
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) STATE.running = false;
            else if (e.type == SDL_EVENT_WINDOW_MINIMIZED) STATE.should_rendering = false;
            else if (e.type == SDL_EVENT_WINDOW_RESTORED || e.type == SDL_EVENT_WINDOW_MAXIMIZED) STATE.should_rendering = true;
        }

        if (!STATE.should_rendering)
        {
            SDL_WaitEventTimeout(nullptr, 100);
            continue;
        }

        uint32_t imageIndex = 0;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        begin_frame(imageIndex, cmd);

        // Build per-frame RenderContext
        RenderContext rctx{};
        rctx.device = ctx_.device;
        rctx.allocator = ctx_.allocator;
        rctx.frameExtent = swapchain_.swapchain_extent;
        rctx.swapchainFormat = swapchain_.swapchain_image_format;
        rctx.swapchainImage = swapchain_.swapchain_images[imageIndex];
        rctx.offscreenImage = swapchain_.drawable_image.image;
        rctx.offscreenImageView = swapchain_.drawable_image.imageView;
        rctx.descriptorAllocator = &globalDescriptorAllocator_;

        renderer_->record(cmd, static_cast<uint32_t>(swapchain_.swapchain_extent.width), static_cast<uint32_t>(swapchain_.swapchain_extent.height), rctx);

        end_frame(imageIndex, cmd);
        STATE.frame_number++;
    }
}

void VulkanEngine::cleanup()
{
    if (renderer_)
    {
        RenderContext rctx{};
        rctx.device = ctx_.device;
        rctx.allocator = ctx_.allocator;
        rctx.frameExtent = swapchain_.swapchain_extent;
        rctx.swapchainFormat = swapchain_.swapchain_image_format;
        rctx.offscreenImage = swapchain_.drawable_image.image;
        rctx.offscreenImageView = swapchain_.drawable_image.imageView;
        rctx.descriptorAllocator = &globalDescriptorAllocator_;
        renderer_->destroy(rctx);
        renderer_.reset();
    }

    // Destroy per-frame resources
    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        if (frames_[i].renderFence) vkDestroyFence(ctx_.device, frames_[i].renderFence, nullptr);
        if (frames_[i].swapchainSemaphore) vkDestroySemaphore(ctx_.device, frames_[i].swapchainSemaphore, nullptr);
        if (frames_[i].renderSemaphore) vkDestroySemaphore(ctx_.device, frames_[i].renderSemaphore, nullptr);
        if (frames_[i].commandPool) vkDestroyCommandPool(ctx_.device, frames_[i].commandPool, nullptr);
    }

    // Destroy swapchain views
    for (auto v : swapchain_.swapchain_image_views) vkDestroyImageView(ctx_.device, v, nullptr);
    if (swapchain_.swapchain) vkDestroySwapchainKHR(ctx_.device, swapchain_.swapchain, nullptr);

    // Destroy allocator and device via mdq
    mdq_.flush();

    if (ctx_.device) vkDestroyDevice(ctx_.device, nullptr);
    if (ctx_.surface) vkDestroySurfaceKHR(ctx_.instance, ctx_.surface, nullptr);
    if (ctx_.instance)
    {
        if (ctx_.debug_messenger)
        {
            auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(ctx_.instance, "vkDestroyDebugUtilsMessengerEXT");
            if (func) func(ctx_.instance, ctx_.debug_messenger, nullptr);
        }
        vkDestroyInstance(ctx_.instance, nullptr);
    }

    if (ctx_.window) SDL_DestroyWindow(ctx_.window);
    SDL_Quit();
}

// Factory for default renderer
#include "renderer_compute_bg.h"

std::unique_ptr<IRenderer> CreateDefaultComputeRenderer()
{
    return std::make_unique<ComputeBackgroundRenderer>();
}
