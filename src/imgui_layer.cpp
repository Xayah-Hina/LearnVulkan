#include "imgui_layer.h"
#include "vk_initializers.h"   // for vkinit:: helpers if you use them
#include "vk_images.h"         // for vkutil::transition_image if you use it

#include <array>

#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult err__ = (x); if (err__ != VK_SUCCESS) { /* simple check */ abort(); } } while(0)
#endif

bool ImGuiLayer::init(SDL_Window* window,
                      VkInstance instance,
                      VkPhysicalDevice physicalDevice,
                      VkDevice device,
                      VkQueue graphicsQueue,
                      uint32_t graphicsQueueFamily,
                      VkFormat swapchainFormat,
                      uint32_t swapchainImageCount)
{
    // 1) Descriptor pool
    std::array<VkDescriptorPoolSize, 11> pool_sizes{{
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    }};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = 1000u * (uint32_t)pool_sizes.size();
    dpci.poolSizeCount = (uint32_t)pool_sizes.size();
    dpci.pPoolSizes = pool_sizes.data();
    VK_CHECK(vkCreateDescriptorPool(device, &dpci, nullptr, &pool_));

    // 2) ImGui context and backends
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion      = VK_API_VERSION_1_3;
    ii.Instance        = instance;
    ii.PhysicalDevice  = physicalDevice;
    ii.Device          = device;
    ii.QueueFamily     = graphicsQueueFamily;
    ii.Queue           = graphicsQueue;
    ii.DescriptorPool  = pool_;
    ii.MinImageCount   = swapchainImageCount;
    ii.ImageCount      = swapchainImageCount;
    ii.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    ii.Allocator       = nullptr;
    ii.CheckVkResultFn = [](VkResult e){ VK_CHECK(e); };

    ii.UseDynamicRendering = true;
    VkPipelineRenderingCreateInfo prci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    prci.colorAttachmentCount = 1;
    prci.pColorAttachmentFormats = &swapchainFormat;
    prci.depthAttachmentFormat   = VK_FORMAT_UNDEFINED;
    prci.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
    ii.PipelineRenderingCreateInfo = prci;

    bool ok = ImGui_ImplVulkan_Init(&ii);
    if (!ok) return false;

    colorFormat_ = swapchainFormat;
    inited_ = true;
    return true;
}

void ImGuiLayer::shutdown(VkDevice device)
{
    if (!inited_) return;
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (pool_) {
        vkDestroyDescriptorPool(device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    inited_ = false;
}

void ImGuiLayer::process_event(const SDL_Event* e)
{
    if (!inited_ || !e) return;
    ImGui_ImplSDL3_ProcessEvent(e);
}

void ImGuiLayer::new_frame()
{
    if (!inited_) return;
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    // Run registered panels
    for (auto& fn : panels_) { fn(); }
}

void ImGuiLayer::render_overlay(VkCommandBuffer cmd,
                                VkImage swapchainImage,
                                VkImageView swapchainView,
                                VkExtent2D extent,
                                VkImageLayout previousLayout)
{
    if (!inited_) return;

    // Transition to COLOR_ATTACHMENT_OPTIMAL
    // You can replace this with your vkutil::transition_image helper
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
    barrier.oldLayout = previousLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.image = swapchainImage;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);

    // Begin dynamic rendering
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView   = swapchainView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.offset = {0, 0};
    ri.renderArea.extent = extent;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;

    vkCmdBeginRendering(cmd, &ri);

    // Build draw data and record it
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);

    // COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void ImGuiLayer::set_min_image_count(uint32_t count)
{
    if (!inited_) return;
    ImGui_ImplVulkan_SetMinImageCount(count);
}
