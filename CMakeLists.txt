cmake_minimum_required(VERSION 3.26)
project(LearnVulkan)

include(cmake/compile_shaders.cmake)
include(cmake/setup_glm.cmake)
include(cmake/setup_imgui.cmake)
include(cmake/setup_sdl3.cmake)
include(cmake/setup_stbimage.cmake)
include(cmake/setup_vkbootstrap.cmake)
include(cmake/setup_vma.cmake)
include(cmake/setup_vulkan.cmake)

set(src_files
        src/vk_engine.cpp
        src/vk_engine.h
        src/renderer_iface.h
        src/imgui_layer.cpp
        src/imgui_layer.h

        src/ext/vk_initializers.cpp
        src/ext/vk_initializers.h
        src/ext/vk_images.cpp
        src/ext/vk_images.h
        src/ext/vk_descriptors.cpp
        src/ext/vk_descriptors.h
        src/ext/vk_pipelines.cpp
        src/ext/vk_pipelines.h

        examples/entrance.cpp
        examples/renderer_compute_bg.cpp
        examples/renderer_compute_bg.h
        examples/renderer_triangle.cpp
        examples/renderer_triangle.h
        examples/renderer_mesh.cpp
        examples/renderer_mesh.h
        examples/renderer_barchart.cpp
        examples/renderer_barchart.h
)
set(VulkanAppName "vulkan_app")
add_executable(${VulkanAppName}
        main.cpp
        ${vkbootstrap_files}
        ${src_files}
)
target_compile_features(${VulkanAppName} PRIVATE cxx_std_20)
target_link_libraries(${VulkanAppName} PRIVATE Vulkan::Vulkan SDL3::SDL3 glm stb_image imgui GPUOpen::VulkanMemoryAllocator)
target_include_directories(${VulkanAppName} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if (MSVC)
    target_compile_options(${VulkanAppName} PRIVATE /W4 /permissive- /Zc:preprocessor)
    add_custom_command(TARGET ${VulkanAppName}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_RUNTIME_DLLS:${VulkanAppName}>
            $<TARGET_FILE_DIR:${VulkanAppName}>
            COMMAND_EXPAND_LISTS
            COMMENT "Copying dependent DLLs to runtime directory"
    )
else()
    target_compile_options(${VulkanAppName} PRIVATE -Wall -Wextra -Wpedantic)
endif()

vkb_attach_to_target(${VulkanAppName})