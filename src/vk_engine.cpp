#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_pipelines.h"
#include "vk_images.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "VkBootstrap.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cmath>
#include <stdexcept>
#include <string>

// Optional: simple VK_CHECK helper
#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult err__ = (x); if (err__ != VK_SUCCESS) { throw std::runtime_error(std::string("Vulkan error, code = ") + std::to_string(err__)); } } while(0)
#endif

// =======================================================
// Constructor / Destructor
// =======================================================
VulkanEngine::VulkanEngine() = default;
VulkanEngine::~VulkanEngine() = default;

// =======================================================
// Initialization
// =======================================================
void VulkanEngine::init() {
    // ----------------------------
    // Context setup
    // ----------------------------
    ctx = std::make_unique<EngineContext>();
    swapchain = std::make_unique<SwapchainSystem>();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    ctx->window = SDL_CreateWindow("Vulkan Engine", STATE.width, STATE.height, SDL_WINDOW_VULKAN);
    if (!ctx->window) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    // ----------------------------
    // Vulkan instance (via VkBootstrap)
    // ----------------------------
    vkb::InstanceBuilder instanceBuilder;
    auto inst_ret = instanceBuilder
        .set_app_name(STATE.name.c_str())
        .request_validation_layers(false)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    vkb::Instance vkb_inst = inst_ret.value();
    ctx->instance = vkb_inst.instance;
    ctx->debug_messenger = vkb_inst.debug_messenger;

    // ----------------------------
    // Surface creation
    // ----------------------------
    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(ctx->window, vkb_inst.instance, nullptr, &surface)) {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
    ctx->surface = surface;

    // ----------------------------
    // Physical device selection
    // ----------------------------
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{vkb_inst};
    vkb::PhysicalDevice physicalDevice = selector
        .set_surface(surface)
        .set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .set_required_features_12(features12)
        .select()
        .value();

    ctx->physical = physicalDevice.physical_device;

    // ----------------------------
    // Logical device creation
    // ----------------------------
    vkb::DeviceBuilder deviceBuilder{physicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();

    ctx->device = vkbDevice.device;
    ctx->graphics_queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    ctx->graphics_queue_family = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // ----------------------------
    // VMA allocator
    // ----------------------------
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = ctx->physical;
    allocatorInfo.device = ctx->device;
    allocatorInfo.instance = vkb_inst.instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &ctx->allocator));
    mdq.push_function([&]() {
        vmaDestroyAllocator(ctx->allocator);
    });

    // ----------------------------
    // Swapchain
    // ----------------------------
    vkb::SwapchainBuilder swapchainBuilder{ctx->physical, ctx->device, surface};
    swapchain->swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{
            .format = swapchain->swapchain_image_format,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // vsync
        .set_desired_extent(STATE.width, STATE.height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();

    swapchain->swapchain_extent = vkbSwapchain.extent;
    swapchain->swapchain = vkbSwapchain.swapchain;
    swapchain->swapchain_images = vkbSwapchain.get_images().value();
    swapchain->swapchain_image_views = vkbSwapchain.get_image_views().value();

    // ----------------------------
    // Drawable offscreen image
    // ----------------------------
    swapchain->drawable_image.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    swapchain->drawable_image.imageExtent = {
        swapchain->swapchain_extent.width,
        swapchain->swapchain_extent.height,
        1
    };

    VkImageUsageFlags drawImageUsages = 0;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimg_info = vkinit::image_create_info(
        swapchain->drawable_image.imageFormat,
        drawImageUsages,
        swapchain->drawable_image.imageExtent
    );

    VmaAllocationCreateInfo rimg_allocinfo{};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(ctx->allocator, &rimg_info, &rimg_allocinfo,
                            &swapchain->drawable_image.image,
                            &swapchain->drawable_image.allocation, nullptr));

    VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(
        swapchain->drawable_image.imageFormat,
        swapchain->drawable_image.image,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    VK_CHECK(vkCreateImageView(ctx->device, &rview_info, nullptr, &swapchain->drawable_image.imageView));

    mdq.push_function([&]() {
        vkDestroyImageView(ctx->device, swapchain->drawable_image.imageView, nullptr);
        vmaDestroyImage(ctx->allocator, swapchain->drawable_image.image,
                        swapchain->drawable_image.allocation);
    });

    // ----------------------------
    // Per frame resources
    // ----------------------------
    VkCommandPoolCreateInfo commandPoolInfo =
        vkinit::command_pool_create_info(ctx->graphics_queue_family,
                                         VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateCommandPool(ctx->device, &commandPoolInfo, nullptr, &frames[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo =
            vkinit::command_buffer_allocate_info(frames[i].commandPool, 1);

        VK_CHECK(vkAllocateCommandBuffers(ctx->device, &cmdAllocInfo, &frames[i].mainCommandBuffer));
    }

    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateFence(ctx->device, &fenceCreateInfo, nullptr, &frames[i].renderFence));
        VK_CHECK(vkCreateSemaphore(ctx->device, &semaphoreCreateInfo, nullptr, &frames[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(ctx->device, &semaphoreCreateInfo, nullptr, &frames[i].renderSemaphore));
    }

    // ----------------------------
    // Descriptor sets
    // ----------------------------
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0f }
    };
    globalDescriptorAllocator.init_pool(ctx->device, 10, sizes);

    DescriptorLayoutBuilder descriptorLayoutBuilder;
    descriptorLayoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    _drawImageDescriptorLayout = descriptorLayoutBuilder.build(ctx->device, VK_SHADER_STAGE_COMPUTE_BIT);

    _drawImageDescriptors = globalDescriptorAllocator.allocate(ctx->device, _drawImageDescriptorLayout);

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imgInfo.imageView = swapchain->drawable_image.imageView;

    VkWriteDescriptorSet drawImageWrite{};
    drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrite.dstBinding = 0;
    drawImageWrite.dstSet = _drawImageDescriptors;
    drawImageWrite.descriptorCount = 1;
    drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrite.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(ctx->device, 1, &drawImageWrite, 0, nullptr);

    mdq.push_function([&]() {
        globalDescriptorAllocator.destroy_pool(ctx->device);
        vkDestroyDescriptorSetLayout(ctx->device, _drawImageDescriptorLayout, nullptr);
    });

    // ----------------------------
    // Compute pipelines
    // ----------------------------
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.flags = 0;
    computeLayout.setLayoutCount = 1;
    computeLayout.pSetLayouts = &_drawImageDescriptorLayout;

    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(ComputePushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    computeLayout.pushConstantRangeCount = 1;
    computeLayout.pPushConstantRanges = &pushConstant;

    VK_CHECK(vkCreatePipelineLayout(ctx->device, &computeLayout, nullptr, &_gradientPipelineLayout));

    VkShaderModule gradientShader;
    if (!vkutil::load_shader_module("./shaders/gradient_color.comp.spv", ctx->device, &gradientShader)) {
        throw std::runtime_error("Failed to load gradient shader");
    }

    VkShaderModule skyShader;
    if (!vkutil::load_shader_module("./shaders/sky.comp.spv", ctx->device, &skyShader)) {
        throw std::runtime_error("Failed to load sky shader");
    }

    // Prepare stage template
    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.flags = 0;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageinfo.pName = "main";
    stageinfo.pSpecializationInfo = nullptr;

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.flags = 0;
    computePipelineCreateInfo.layout = _gradientPipelineLayout;

    // Gradient pipeline
    stageinfo.module = gradientShader;                     // set module first
    computePipelineCreateInfo.stage = stageinfo;           // then assign to create-info
    ComputeEffect gradient{};
    gradient.layout = _gradientPipelineLayout;
    gradient.name = "gradient";
    gradient.data = {};
    gradient.data.data1 = glm::vec4(1.f, 0.f, 0.f, 1.f);
    gradient.data.data2 = glm::vec4(0.f, 0.f, 1.f, 1.f);
    VK_CHECK(vkCreateComputePipelines(ctx->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

    // Sky pipeline
    stageinfo.module = skyShader;                          // set module first
    computePipelineCreateInfo.stage = stageinfo;           // then assign to create-info
    ComputeEffect sky{};
    sky.layout = _gradientPipelineLayout;
    sky.name = "sky";
    sky.data = {};
    sky.data.data1 = glm::vec4(0.1f, 0.2f, 0.4f, 0.97f);
    VK_CHECK(vkCreateComputePipelines(ctx->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

    backgroundEffects.push_back(gradient);
    backgroundEffects.push_back(sky);

    // Shader modules can be destroyed after pipelines are created
    vkDestroyShaderModule(ctx->device, gradientShader, nullptr);
    vkDestroyShaderModule(ctx->device, skyShader, nullptr);

    // Defer pipeline and layout destruction
    mdq.push_function([=]() {
        vkDestroyPipeline(ctx->device, sky.pipeline, nullptr);
        vkDestroyPipeline(ctx->device, gradient.pipeline, nullptr);
        vkDestroyPipelineLayout(ctx->device, _gradientPipelineLayout, nullptr);
    });

    // ----------------------------
    // Mark state
    // ----------------------------
    STATE.initialized = true;
    STATE.running = true;
    STATE.should_rendering = true;
}

// =======================================================
// Cleanup
// =======================================================
void VulkanEngine::cleanup() {
    STATE.initialized = false;
    STATE.running = false;
    STATE.should_rendering = false;
}

// =======================================================
// Main loop
// =======================================================
void VulkanEngine::run() {
    SDL_Event e{};
    while (STATE.running) {
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    STATE.running = false;
                    break;
                case SDL_EVENT_WINDOW_MINIMIZED:
                    STATE.should_rendering = false;
                    break;
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    STATE.should_rendering = true;
                    break;
                default:
                    break;
            }
        }

        if (STATE.should_rendering) {
            draw();
        } else {
            SDL_WaitEventTimeout(nullptr, 100);
        }
    }
}

// =======================================================
// Render a frame
// =======================================================
void VulkanEngine::draw() {
    // Wait for GPU to finish the last frame
    VK_CHECK(vkWaitForFences(ctx->device, 1, &get_current_frame().renderFence, VK_TRUE, 1000000000));
    get_current_frame().deletionQueue.flush();

    // Acquire swapchain image
    uint32_t swapchainImageIndex = 0;
    VkResult acquireRes = vkAcquireNextImageKHR(
        ctx->device,
        swapchain->swapchain,
        1000000000,
        get_current_frame().swapchainSemaphore,
        nullptr,
        &swapchainImageIndex
    );

    if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain recreation is not implemented in this sample
        throw std::runtime_error("NOT IMPLEMENTED: rebuild_swapchain()");
    }
    VK_CHECK(acquireRes);

    // Reset sync for this frame
    VK_CHECK(vkResetFences(ctx->device, 1, &get_current_frame().renderFence));
    VK_CHECK(vkResetCommandBuffer(get_current_frame().mainCommandBuffer, 0));

    VkCommandBuffer cmd = get_current_frame().mainCommandBuffer;

    // Begin recording
    VkCommandBufferBeginInfo cmdBeginInfo =
        vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // 1) Transition drawable image to GENERAL
    vkutil::transition_image(cmd, swapchain->drawable_image.image,
                             VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    // 2) Run compute effect
    ComputeEffect& effect = backgroundEffects[STATE.current_background_effect];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);
    vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(ComputePushConstants), &effect.data);

    // Dispatch size assumes local size of 16x16 in the shader
    uint32_t gx = static_cast<uint32_t>(std::ceil(STATE.width  / 16.0));
    uint32_t gy = static_cast<uint32_t>(std::ceil(STATE.height / 16.0));
    vkCmdDispatch(cmd, gx, gy, 1);

    // 3) Copy drawable to swapchain image for presentation
    vkutil::transition_image(cmd, swapchain->drawable_image.image,
                             VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(cmd, swapchain->swapchain_images[swapchainImageIndex],
                             VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkutil::copy_image_to_image(cmd,
                                swapchain->drawable_image.image,
                                swapchain->swapchain_images[swapchainImageIndex],
                                VkExtent2D(STATE.width, STATE.height),
                                VkExtent2D(STATE.width, STATE.height));

    vkutil::transition_image(cmd, swapchain->swapchain_images[swapchainImageIndex],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit work
    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
        get_current_frame().swapchainSemaphore
    );
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        get_current_frame().renderSemaphore
    );
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);

    VK_CHECK(vkQueueSubmit2(ctx->graphics_queue, 1, &submit, get_current_frame().renderFence));

    // Present
    VkPresentInfoKHR presentInfo = vkinit::present_info();
    presentInfo.pSwapchains = &swapchain->swapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pWaitSemaphores = &get_current_frame().renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(ctx->graphics_queue, &presentInfo));

    STATE.frame_number++;
}
