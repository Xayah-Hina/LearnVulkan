cmake_minimum_required(VERSION 3.26)
project(LearnVulkan)

include(cmake/setup_sdl.cmake)
find_package(Vulkan REQUIRED)

set(vkbootstrap_files
        vkbootstrap/VkBootstrap.cpp
        vkbootstrap/VkBootstrap.h
        vkbootstrap/VkBootstrapDispatch.h
)
set(src_files
        src/vulkan_engine.cpp
        src/vulkan_engine.h
        src/vulkan_initializers.cpp
        src/vulkan_initializers.h
)
set(VulkanAppName "LearnVulkan")
add_executable(${VulkanAppName}
        main.cpp
        ${vkbootstrap_files}
        ${src_files}
)
target_compile_features(${VulkanAppName} PRIVATE cxx_std_20)
target_link_libraries(${VulkanAppName} PRIVATE Vulkan::Vulkan SDL3::SDL3)
target_include_directories(${VulkanAppName} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if (MSVC)
    target_compile_options(${VulkanAppName} PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
    target_compile_options(${VulkanAppName} PRIVATE -Wall -Wextra -Wpedantic)
endif()