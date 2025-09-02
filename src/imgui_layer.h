#ifndef IMGUI_LAYER_H
#define IMGUI_LAYER_H

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <functional>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"

class ImGuiLayer
{
public:
    // Initialize backends and descriptor pool
    // Returns true on success
    bool init(SDL_Window* window,
              VkInstance instance,
              VkPhysicalDevice physicalDevice,
              VkDevice device,
              VkQueue graphicsQueue,
              uint32_t graphicsQueueFamily,
              VkFormat swapchainFormat,
              uint32_t swapchainImageCount);

    // Shutdown backends and destroy descriptor pool
    void shutdown(VkDevice device);

    // Feed SDL event to ImGui platform backend
    void process_event(const SDL_Event* e);

    // Begin a new UI frame and run registered panels
    void new_frame();

    // Record ImGui draw data on top of the current swapchain image
    // Assumes the image is in TRANSFER_DST_OPTIMAL
    // Transitions: TRANSFER_DST_OPTIMAL -> COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
    void render_overlay(VkCommandBuffer cmd,
                        VkImage swapchainImage,
                        VkImageView swapchainView,
                        VkExtent2D extent,
                        VkImageLayout previousLayout);

    // Optional: register UI panels that will be called every frame
    using PanelFn = std::function<void()>;
    void add_panel(PanelFn fn) { panels_.push_back(std::move(fn)); }

    // Update image count when swapchain is recreated
    void set_min_image_count(uint32_t count);

private:
    VkDescriptorPool pool_{};
    bool inited_{false};
    VkFormat colorFormat_{};
    std::vector<PanelFn> panels_;
};


#endif //IMGUI_LAYER_H
