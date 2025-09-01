#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_pipelines.h"
#include "vk_images.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "VkBootstrap.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

VulkanEngine::VulkanEngine() = default;
VulkanEngine::~VulkanEngine() = default;

void VulkanEngine::init()
{
    this->ctx = std::make_unique<EngineContext>();
    this->swapchain = std::make_unique<SwapchainSystem>();

    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(std::string("SDL_Init(SDL_INIT_VIDEO) failed: ") + SDL_GetError());
    if (!(this->ctx->window = SDL_CreateWindow("Vulkan Engine", STATE.width, STATE.height, SDL_WINDOW_VULKAN)))
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());


    vkb::InstanceBuilder instanceBuilder;
    auto inst_ret = instanceBuilder
                    .set_app_name(this->STATE.name.c_str())
                    .request_validation_layers(false)
                    .use_default_debug_messenger()
                    .require_api_version(1, 3, 0)
                    .build();
    vkb::Instance vkb_inst = inst_ret.value();
    this->ctx->instance = vkb_inst.instance;
    this->ctx->debug_messenger = vkb_inst.debug_messenger;


    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(this->ctx->window, vkb_inst.instance, nullptr, &surface))
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    this->ctx->surface = surface;


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
                                         .select().value();
    this->ctx->physical = physicalDevice.physical_device;


    vkb::DeviceBuilder deviceBuilder{physicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();
    this->ctx->device = vkbDevice.device;
    this->ctx->graphics_queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    this->ctx->graphics_queue_family = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();


    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physicalDevice.physical_device;
    allocatorInfo.device = vkbDevice.device;
    allocatorInfo.instance = vkb_inst.instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &this->ctx->allocator);
    this->mdq.push_function([&]() { vmaDestroyAllocator(this->ctx->allocator); });


    vkb::SwapchainBuilder swapchainBuilder{physicalDevice.physical_device, vkbDevice.device, surface};
    this->swapchain->swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;
    vkb::Swapchain vkbSwapchain = swapchainBuilder
                                  //.use_default_format_selection()
                                  .set_desired_format(VkSurfaceFormatKHR{.format = this->swapchain->swapchain_image_format, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                                  //use vsync present mode
                                  .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                  .set_desired_extent(STATE.width, STATE.height)
                                  .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                                  .build()
                                  .value();
    this->swapchain->swapchain_extent = vkbSwapchain.extent;
    this->swapchain->swapchain = vkbSwapchain.swapchain;
    this->swapchain->swapchain_images = vkbSwapchain.get_images().value();
    this->swapchain->swapchain_image_views = vkbSwapchain.get_image_views().value();
    this->swapchain->drawable_image.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    this->swapchain->drawable_image.imageExtent.width = this->swapchain->swapchain_extent.width;
    this->swapchain->drawable_image.imageExtent.height = this->swapchain->swapchain_extent.height;
    this->swapchain->drawable_image.imageExtent.depth = 1;
    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageCreateInfo rimg_info = vkinit::image_create_info(this->swapchain->drawable_image.imageFormat, drawImageUsages, this->swapchain->drawable_image.imageExtent);
    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vmaCreateImage(this->ctx->allocator, &rimg_info, &rimg_allocinfo, &this->swapchain->drawable_image.image, &this->swapchain->drawable_image.allocation, nullptr);
    VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(this->swapchain->drawable_image.imageFormat, this->swapchain->drawable_image.image, VK_IMAGE_ASPECT_COLOR_BIT);
    vkCreateImageView(this->ctx->device, &rview_info, nullptr, &this->swapchain->drawable_image.imageView);
    this->mdq.push_function([&]()
    {
        vkDestroyImageView(this->ctx->device, this->swapchain->drawable_image.imageView, nullptr);
        vmaDestroyImage(this->ctx->allocator, this->swapchain->drawable_image.image, this->swapchain->drawable_image.allocation);
    });


    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(this->ctx->graphics_queue_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        vkCreateCommandPool(this->ctx->device, &commandPoolInfo, nullptr, &frames[i].commandPool);
        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(frames[i].commandPool, 1);
        vkAllocateCommandBuffers(this->ctx->device, &cmdAllocInfo, &frames[i].mainCommandBuffer);
    }


    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();
    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        vkCreateFence(this->ctx->device, &fenceCreateInfo, nullptr, &frames[i].renderFence);
        vkCreateSemaphore(this->ctx->device, &semaphoreCreateInfo, nullptr, &frames[i].swapchainSemaphore);
        vkCreateSemaphore(this->ctx->device, &semaphoreCreateInfo, nullptr, &frames[i].renderSemaphore);
    }


    std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
    {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}
    };
    globalDescriptorAllocator.init_pool(this->ctx->device, 10, sizes);
    {
        DescriptorLayoutBuilder descriptorLayoutBuilder;
        descriptorLayoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        _drawImageDescriptorLayout = descriptorLayoutBuilder.build(this->ctx->device, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    _drawImageDescriptors = globalDescriptorAllocator.allocate(this->ctx->device, _drawImageDescriptorLayout);
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imgInfo.imageView = this->swapchain->drawable_image.imageView;
    VkWriteDescriptorSet drawImageWrite = {};
    drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrite.pNext = nullptr;
    drawImageWrite.dstBinding = 0;
    drawImageWrite.dstSet = _drawImageDescriptors;
    drawImageWrite.descriptorCount = 1;
    drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrite.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(this->ctx->device, 1, &drawImageWrite, 0, nullptr);
    this->mdq.push_function([&]()
    {
        globalDescriptorAllocator.destroy_pool(this->ctx->device);
        vkDestroyDescriptorSetLayout(this->ctx->device, _drawImageDescriptorLayout, nullptr);
    });


    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
    computeLayout.setLayoutCount = 1;
    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(ComputePushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    computeLayout.pPushConstantRanges = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;
    vkCreatePipelineLayout(this->ctx->device, &computeLayout, nullptr, &_gradientPipelineLayout);
    VkShaderModule gradientShader;
    if (!vkutil::load_shader_module("./shaders/gradient_color.comp.spv", this->ctx->device, &gradientShader))
        throw std::runtime_error("Error when building the compute shader \n");
    VkShaderModule skyShader;
    if (!vkutil::load_shader_module("./shaders/sky.comp.spv", this->ctx->device, &skyShader))
        throw std::runtime_error("Error when building the compute shader \n");
    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageinfo.module = gradientShader;
    stageinfo.pName = "main";
    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = _gradientPipelineLayout;
    computePipelineCreateInfo.stage = stageinfo;
    ComputeEffect gradient;
    gradient.layout = _gradientPipelineLayout;
    gradient.name = "gradient";
    gradient.data = {};
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);
    vkCreateComputePipelines(this->ctx->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline);
    computePipelineCreateInfo.stage.module = skyShader;
    ComputeEffect sky;
    sky.layout = _gradientPipelineLayout;
    sky.name = "sky";
    sky.data = {};
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);
    vkCreateComputePipelines(this->ctx->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline);
    backgroundEffects.push_back(gradient);
    backgroundEffects.push_back(sky);
    vkDestroyShaderModule(this->ctx->device, gradientShader, nullptr);
    vkDestroyShaderModule(this->ctx->device, skyShader, nullptr);
    this->mdq.push_function([=]()
    {
        vkDestroyPipelineLayout(this->ctx->device, _gradientPipelineLayout, nullptr);
        vkDestroyPipeline(this->ctx->device, sky.pipeline, nullptr);
        vkDestroyPipeline(this->ctx->device, gradient.pipeline, nullptr);
    });


    this->STATE.initialized = true;
    this->STATE.running = true;
    this->STATE.should_rendering = true;
}

void VulkanEngine::cleanup()
{
    STATE.initialized = false;
    STATE.running = false;
    STATE.should_rendering = false;
}

void VulkanEngine::run()
{
    SDL_Event e{};
    while (STATE.running)
    {
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_EVENT_QUIT:
                STATE.running = false;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                STATE.running = false;
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                STATE.should_rendering = false;
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                STATE.should_rendering = true;
                break;
            case SDL_EVENT_WINDOW_MAXIMIZED:
                STATE.should_rendering = true;
                break;
            default:
                break;
            }
        }
        if (STATE.should_rendering)
            this->draw();
        else
            SDL_WaitEventTimeout(nullptr, 100);
    }
}

void VulkanEngine::draw()
{
    vkWaitForFences(this->ctx->device, 1, &get_current_frame().renderFence, true, 1000000000);
    get_current_frame().deletionQueue.flush();
    uint32_t swapchainImageIndex;
    VkResult e = vkAcquireNextImageKHR(this->ctx->device, this->swapchain->swapchain, 1000000000, get_current_frame().swapchainSemaphore, nullptr, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR)
    {
        throw std::runtime_error("NOT IMPLEMENTED: rebuild_swapchain()");
        return;
    }

    vkResetFences(this->ctx->device, 1, &get_current_frame().renderFence);
    vkResetCommandBuffer(get_current_frame().mainCommandBuffer, 0);
    VkCommandBuffer cmd = get_current_frame().mainCommandBuffer;
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    vkBeginCommandBuffer(cmd, &cmdBeginInfo);
    vkutil::transition_image(cmd, this->swapchain->drawable_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    ComputeEffect& effect = backgroundEffects[STATE.current_background_effect];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);
    vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);
    vkCmdDispatch(cmd, std::ceil(STATE.width / 16.0), std::ceil(STATE.height / 16.0), 1);

    vkutil::transition_image(cmd, this->swapchain->drawable_image.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(cmd, this->swapchain->swapchain_images[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkutil::copy_image_to_image(cmd, this->swapchain->drawable_image.image, this->swapchain->swapchain_images[swapchainImageIndex], VkExtent2D(STATE.width, STATE.height), VkExtent2D(STATE.width, STATE.height));
    vkutil::transition_image(cmd, this->swapchain->swapchain_images[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkEndCommandBuffer(cmd);

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame().swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame().renderSemaphore);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);
    vkQueueSubmit2(this->ctx->graphics_queue, 1, &submit, get_current_frame().renderFence);


    VkPresentInfoKHR presentInfo = vkinit::present_info();
    presentInfo.pSwapchains = &this->swapchain->swapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pWaitSemaphores = &get_current_frame().renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices = &swapchainImageIndex;
    VkResult presentResult = vkQueuePresentKHR(this->ctx->graphics_queue, &presentInfo);


    STATE.frame_number++;
}
