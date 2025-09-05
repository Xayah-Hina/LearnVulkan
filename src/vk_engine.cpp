#include "vk_engine.h"

#include <SDL3/SDL_vulkan.h>

#include <stdexcept>
#include <cmath>
#include <array>

#include "ext/vk_initializers.h"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "VkBootstrap.h"
#include "imgui.h"

#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult err__ = (x); if (err__ != VK_SUCCESS) { throw std::runtime_error(std::string("Vulkan error ") + std::to_string(err__)); } } while(0)
#endif
#ifndef IF_NOT_NULL_DO
#define IF_NOT_NULL_DO(ptr, stmt) do{ if((ptr)!=nullptr){ stmt; } }while(0)
#endif
#ifndef IF_NOT_NULL_DO_AND_SET
#define IF_NOT_NULL_DO_AND_SET(ptr, stmt, val) do{ if((ptr)!=nullptr){ stmt; (ptr)=val; } }while(0)
#endif
#ifndef REQUIRE_TRUE
#define REQUIRE_TRUE(expr,msg) do{ if(!(expr)) throw std::runtime_error(std::string("Check failed: ")+#expr+" | "+(msg)); } while(0)
#endif
#ifndef REQUIRE_PTR
#define REQUIRE_PTR(expr,msg)  ([&](){ auto* _p_=(expr); if(!_p_) throw std::runtime_error(std::string("Null returned: ")+#expr+" | "+(msg)); return _p_; }())
#endif
#ifndef REQUIRE_OK
#define REQUIRE_OK(expr,ok,msg) ([&](){ auto _rv_=(expr); if(!(_rv_==(ok))) throw std::runtime_error(std::string("Unexpected return from ")+#expr+" got="+std::to_string(static_cast<long long>(_rv_))+" expected="+std::to_string(static_cast<long long>(ok))+" | "+(msg)); return _rv_; }())
#endif

void VulkanEngine::init()
{
    create_context(state_.width, state_.height, state_.name.c_str());
    create_swapchain(state_.width, state_.height);
    create_offscreen_drawable(state_.width, state_.height);
    create_command_buffers();
    create_renderer();
    create_imgui();

    state_.initialized = true;
    state_.running = true;
    state_.should_rendering = true;
}

void VulkanEngine::run()
{
    SDL_Event e{};
    while (state_.running)
    {
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                state_.running = false;
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                state_.should_rendering = false;
                break;

            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
                state_.should_rendering = true;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                state_.resize_requested = true;
                break;

            default:
                break;
            }

            IF_NOT_NULL_DO(ui_, ui_->process_event(&e));
        }

        if (!state_.should_rendering)
        {
            SDL_WaitEventTimeout(nullptr, 100);
            continue;
        }

        // handle deferred resize
        if (state_.resize_requested)
        {
            recreate_swapchain();
            continue; // start next frame
        }

        uint32_t imageIndex = 0;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        begin_frame(imageIndex, cmd);

        // if swapchain was out of date, begin_frame() returns early
        if (cmd == VK_NULL_HANDLE)
        {
            // try to rebuild now
            if (state_.resize_requested) recreate_swapchain();
            continue;
        }

        // Build per-frame RenderContext
        RenderContext rctx{};
        rctx.device = ctx_.device;
        rctx.allocator = ctx_.allocator;
        rctx.frameExtent = swapchain_.swapchain_extent;
        rctx.swapchainFormat = swapchain_.swapchain_image_format;
        rctx.swapchainImage = swapchain_.swapchain_images[imageIndex];
        rctx.offscreenImage = swapchain_.drawable_image.image;
        rctx.offscreenImageView = swapchain_.drawable_image.imageView;
        rctx.descriptorAllocator = &ctx_.descriptor_allocator;
        rctx.depthImage = swapchain_.depth_image.image;
        rctx.depthImageView = swapchain_.depth_image.imageView;
        rctx.graphics_queue = ctx_.graphics_queue;
        rctx.graphics_queue_family = ctx_.graphics_queue_family;

        renderer_->record(cmd, static_cast<uint32_t>(swapchain_.swapchain_extent.width), static_cast<uint32_t>(swapchain_.swapchain_extent.height), rctx);

        if (ui_)
        {
            ui_->new_frame();
            if (renderer_) renderer_->on_imgui();
            ui_->render_overlay(cmd,
                                swapchain_.swapchain_images[imageIndex],
                                swapchain_.swapchain_image_views[imageIndex],
                                swapchain_.swapchain_extent,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        end_frame(imageIndex, cmd);
        state_.frame_number++;
    }
}

void VulkanEngine::cleanup()
{
    vkDeviceWaitIdle(ctx_.device);
    destroy_command_buffers();
    mdq_.flush();
    destroy_context();
}

void VulkanEngine::create_context(int window_width, int window_height, const char* app_name)
{
    // 1. create VkInstance + VkDebugUtilsMessengerEXT
    vkb::Instance vkb_inst = vkb::InstanceBuilder()
                             .set_app_name(app_name)
                             .request_validation_layers(false)
                             .use_default_debug_messenger()
                             .require_api_version(1, 3, 0)
                             .build().value();
    ctx_.instance = vkb_inst.instance;
    ctx_.debug_messenger = vkb_inst.debug_messenger;


    // 2. create SDL3 window and VkSurfaceKHR
    REQUIRE_TRUE(SDL_Init(SDL_INIT_VIDEO), std::string("SDL_Init failed: ") + SDL_GetError());
    REQUIRE_PTR(ctx_.window = SDL_CreateWindow("Vulkan Engine", window_width, window_height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE), std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    REQUIRE_TRUE(SDL_Vulkan_CreateSurface(ctx_.window, ctx_.instance, nullptr, &ctx_.surface), std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());


    // 3. select VkPhysicalDevice and create VkDevice + VkQueue
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.bufferDeviceAddress = VK_TRUE;
    f12.descriptorIndexing = VK_TRUE;
    vkb::PhysicalDevice phys = vkb::PhysicalDeviceSelector(vkb_inst)
                               .set_surface(ctx_.surface)
                               .set_minimum_version(1, 3)
                               .set_required_features_13(f13)
                               .set_required_features_12(f12)
                               .select().value();
    ctx_.physical = phys.physical_device;
    vkb::Device vkbDev = vkb::DeviceBuilder(phys)
                         .build().value();
    ctx_.device = vkbDev.device;
    ctx_.graphics_queue = vkbDev.get_queue(vkb::QueueType::graphics).value();
    ctx_.graphics_queue_family = vkbDev.get_queue_index(vkb::QueueType::graphics).value();


    // 4. create VmaAllocator and VkDescriptorPool
    VmaAllocatorCreateInfo ac{};
    ac.physicalDevice = ctx_.physical;
    ac.device = ctx_.device;
    ac.instance = ctx_.instance;
    ac.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    VK_CHECK(vmaCreateAllocator(&ac, &ctx_.allocator));
    mdq_.push_function([&]() { vmaDestroyAllocator(ctx_.allocator); });
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0f}};
    ctx_.descriptor_allocator.init_pool(ctx_.device, 10, sizes);
    mdq_.push_function([&]() { ctx_.descriptor_allocator.destroy_pool(ctx_.device); });
}

void VulkanEngine::destroy_context()
{
    mdq_.flush();
    IF_NOT_NULL_DO_AND_SET(ctx_.device, vkDestroyDevice(ctx_.device, nullptr), nullptr);
    IF_NOT_NULL_DO_AND_SET(ctx_.surface, vkDestroySurfaceKHR(ctx_.instance, ctx_.surface, nullptr), nullptr);
    IF_NOT_NULL_DO_AND_SET(ctx_.window, SDL_DestroyWindow(ctx_.window), nullptr);
    IF_NOT_NULL_DO_AND_SET(ctx_.debug_messenger,
                           { auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>( vkGetInstanceProcAddr(ctx_.instance, "vkDestroyDebugUtilsMessengerEXT"));
                           if (ctx_.instance && func) func(ctx_.instance, ctx_.debug_messenger, nullptr); }, nullptr);
    IF_NOT_NULL_DO_AND_SET(ctx_.instance, vkDestroyInstance(ctx_.instance, nullptr), nullptr);
    SDL_Quit();
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
    swapchain_.swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;
    vkb::Swapchain sc = vkb::SwapchainBuilder(ctx_.physical, ctx_.device, ctx_.surface)
                        .set_desired_format(VkSurfaceFormatKHR{swapchain_.swapchain_image_format, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                        .set_desired_extent(width, height)
                        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                        .build().value();
    swapchain_.swapchain = sc.swapchain;
    swapchain_.swapchain_extent = sc.extent;
    swapchain_.swapchain_images = sc.get_images().value();
    swapchain_.swapchain_image_views = sc.get_image_views().value();

    mdq_.push_function([&]()
    {
        destroy_swapchain();
    });
}

void VulkanEngine::destroy_swapchain()
{
    // IMPORTANT NOTE: DO NOT MANUALLY DESTROY swapchain_.swapchain_images, they are owned by the swapchain
    for (auto v : swapchain_.swapchain_image_views)
        IF_NOT_NULL_DO_AND_SET(v, vkDestroyImageView(ctx_.device, v, nullptr), VK_NULL_HANDLE);
    swapchain_.swapchain_image_views.clear();
    swapchain_.swapchain_images.clear();
    IF_NOT_NULL_DO_AND_SET(swapchain_.swapchain, vkDestroySwapchainKHR(ctx_.device, swapchain_.swapchain, nullptr), VK_NULL_HANDLE);
}

void VulkanEngine::recreate_swapchain()
{
    vkDeviceWaitIdle(ctx_.device);
    destroy_swapchain();

    int w = 0, h = 0;
    SDL_GetWindowSize(ctx_.window, &w, &h);
    w = std::max(1, w);
    h = std::max(1, h);

    create_swapchain((uint32_t)w, (uint32_t)h);

    RenderContext rctx{};
    rctx.device = ctx_.device;
    rctx.allocator = ctx_.allocator;
    rctx.frameExtent = swapchain_.swapchain_extent;
    rctx.swapchainFormat = swapchain_.swapchain_image_format;
    rctx.offscreenImage = swapchain_.drawable_image.image;
    rctx.offscreenImageView = swapchain_.drawable_image.imageView;
    rctx.descriptorAllocator = &ctx_.descriptor_allocator;

    IF_NOT_NULL_DO(renderer_, renderer_->on_swapchain_resized(rctx));
    IF_NOT_NULL_DO(ui_, ui_->set_min_image_count(static_cast<uint32_t>(swapchain_.swapchain_images.size())));

    state_.resize_requested = false;
}

void VulkanEngine::create_offscreen_drawable(uint32_t width, uint32_t height)
{
    VkExtent3D imageExtent = {width, height, 1};
    {
        VkFormat imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT
            | VK_IMAGE_USAGE_STORAGE_BIT
            | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        VkImageCreateInfo imgci = vkinit::image_create_info(imageFormat, usage, imageExtent);
        VmaAllocationCreateInfo ainfo{};
        ainfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        ainfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vmaCreateImage(ctx_.allocator, &imgci, &ainfo, &swapchain_.drawable_image.image, &swapchain_.drawable_image.allocation, nullptr));
        VkImageViewCreateInfo viewci = vkinit::imageview_create_info(imageFormat, swapchain_.drawable_image.image, VK_IMAGE_ASPECT_COLOR_BIT);
        VK_CHECK(vkCreateImageView(ctx_.device, &viewci, nullptr, &swapchain_.drawable_image.imageView));
        swapchain_.drawable_image.imageFormat = imageFormat;
        swapchain_.drawable_image.imageExtent = imageExtent;
    }

    {
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        VkImageCreateInfo imgci = vkinit::image_create_info(depthFormat, usage, imageExtent);
        VmaAllocationCreateInfo ainfo{};
        ainfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        ainfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vmaCreateImage(ctx_.allocator, &imgci, &ainfo, &swapchain_.depth_image.image, &swapchain_.depth_image.allocation, nullptr));
        VkImageViewCreateInfo viewci = vkinit::imageview_create_info(depthFormat, swapchain_.depth_image.image, VK_IMAGE_ASPECT_DEPTH_BIT);
        VK_CHECK(vkCreateImageView(ctx_.device, &viewci, nullptr, &swapchain_.depth_image.imageView));
        swapchain_.depth_image.imageFormat = depthFormat;
        swapchain_.depth_image.imageExtent = imageExtent;
    }

    mdq_.push_function([&]()
    {
        destroy_offscreen_drawable();
    });
}

void VulkanEngine::destroy_offscreen_drawable()
{
    IF_NOT_NULL_DO_AND_SET(swapchain_.drawable_image.imageView, vkDestroyImageView(ctx_.device, swapchain_.drawable_image.imageView, nullptr), VK_NULL_HANDLE);
    IF_NOT_NULL_DO_AND_SET(swapchain_.drawable_image.image, vmaDestroyImage(ctx_.allocator, swapchain_.drawable_image.image, swapchain_.drawable_image.allocation), VK_NULL_HANDLE);
    swapchain_.drawable_image = {};

    IF_NOT_NULL_DO_AND_SET(swapchain_.depth_image.imageView, vkDestroyImageView(ctx_.device, swapchain_.depth_image.imageView, nullptr), VK_NULL_HANDLE);
    IF_NOT_NULL_DO_AND_SET(swapchain_.depth_image.image, vmaDestroyImage(ctx_.allocator, swapchain_.depth_image.image, swapchain_.depth_image.allocation), VK_NULL_HANDLE);
    swapchain_.depth_image = {};
}

void VulkanEngine::create_command_buffers()
{
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
}

void VulkanEngine::destroy_command_buffers()
{
    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        frames_[i].deletionQueue.flush();
        IF_NOT_NULL_DO_AND_SET(frames_[i].renderFence, vkDestroyFence(ctx_.device, frames_[i].renderFence, nullptr), VK_NULL_HANDLE);
        IF_NOT_NULL_DO_AND_SET(frames_[i].swapchainSemaphore, vkDestroySemaphore(ctx_.device, frames_[i].swapchainSemaphore, nullptr), VK_NULL_HANDLE);
        IF_NOT_NULL_DO_AND_SET(frames_[i].renderSemaphore, vkDestroySemaphore(ctx_.device, frames_[i].renderSemaphore, nullptr), VK_NULL_HANDLE);
        IF_NOT_NULL_DO_AND_SET(frames_[i].commandPool, vkDestroyCommandPool(ctx_.device, frames_[i].commandPool, nullptr), VK_NULL_HANDLE);
    }
}

void VulkanEngine::begin_frame(uint32_t& imageIndex, VkCommandBuffer& cmd)
{
    FrameData& fr = frames_[state_.frame_number % FRAME_OVERLAP];

    VK_CHECK(vkWaitForFences(ctx_.device, 1, &fr.renderFence, VK_TRUE, 1000000000));
    fr.deletionQueue.flush();

    VkResult acq = vkAcquireNextImageKHR(ctx_.device, swapchain_.swapchain, 1000000000, fr.swapchainSemaphore, nullptr, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR)
    {
        state_.resize_requested = true;
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

    FrameData& fr = frames_[state_.frame_number % FRAME_OVERLAP];

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

void VulkanEngine::create_renderer()
{
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
    rctx.descriptorAllocator = &ctx_.descriptor_allocator;
    rctx.graphics_queue = ctx_.graphics_queue;
    rctx.graphics_queue_family = ctx_.graphics_queue_family;
    renderer_->initialize(rctx);

    mdq_.push_function([&]()
    {
        destroy_renderer();
    });
}

void VulkanEngine::destroy_renderer()
{
    IF_NOT_NULL_DO_AND_SET(renderer_, {
                           RenderContext rctx{};
                           rctx.device = ctx_.device;
                           rctx.allocator = ctx_.allocator;
                           rctx.frameExtent = swapchain_.swapchain_extent;
                           rctx.swapchainFormat = swapchain_.swapchain_image_format;
                           rctx.offscreenImage = swapchain_.drawable_image.image;
                           rctx.offscreenImageView = swapchain_.drawable_image.imageView;
                           rctx.descriptorAllocator = &ctx_.descriptor_allocator;
                           renderer_->destroy(rctx);
                           renderer_.reset();
                           }, nullptr);
}

void VulkanEngine::create_imgui()
{
    ui_ = std::make_unique<ImGuiLayer>();
    bool ok = ui_->init(ctx_.window,
                        ctx_.instance,
                        ctx_.physical,
                        ctx_.device,
                        ctx_.graphics_queue,
                        ctx_.graphics_queue_family,
                        swapchain_.swapchain_image_format,
                        static_cast<uint32_t>(swapchain_.swapchain_images.size()));
    if (!ok) throw std::runtime_error("ImGuiLayer init failed");

    // Register a simple panel that displays the swapchain extent
    ui_->add_panel([this]()
    {
        ImGui::Begin("Swapchain");

        ImGui::Text("FPS: %.1f", 1.0f / ImGui::GetIO().DeltaTime);

        const VkExtent2D sc = swapchain_.swapchain_extent;
        ImGui::Text("Extent: %u x %u", sc.width, sc.height);

        // Compare with the previous frame to detect changes
        static VkExtent2D prev{~0u, ~0u}; // init to sentinel
        const bool changed = (prev.width != sc.width) || (prev.height != sc.height);
        ImGui::Text("Changed this frame: %s", changed ? "Yes" : "No");
        prev = sc;

        // Optional extra info
        ImGui::Separator();
        ImGui::Text("Images: %zu", swapchain_.swapchain_images.size());
        ImGui::Text("Format: 0x%08X", (uint32_t)swapchain_.swapchain_image_format);

        // Optional: window logical vs pixel size (helps debugging DPI vs extent)
        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(ctx_.window, &win_w, &win_h);
        int px_w = 0, px_h = 0;
        SDL_GetWindowSizeInPixels(ctx_.window, &px_w, &px_h);
        ImGui::Separator();
        ImGui::Text("Window logical: %d x %d", win_w, win_h);
        ImGui::Text("Window pixels : %d x %d", px_w, px_h);

        ImGui::End();
    });
}

void VulkanEngine::destroy_imgui()
{
    IF_NOT_NULL_DO_AND_SET(ui_, { ui_->shutdown(ctx_.device); ui_.reset(); }, nullptr);
}

// Factory for default renderer
#include "renderer_compute_bg.h"
#include "renderer_triangle.h"
#include "renderer_mesh.h"

std::unique_ptr<IRenderer> CreateDefaultComputeRenderer()
{
    return std::make_unique<ComputeBackgroundRenderer>();
    // return std::make_unique<TriangleRenderer>();
    // return std::make_unique<MeshRenderer>();
}
