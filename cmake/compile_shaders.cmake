find_program(GLSL_VALIDATOR glslangValidator HINTS
        $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/Bin32 /usr/local/bin /usr/bin)
if (NOT GLSL_VALIDATOR)
    message(FATAL_ERROR "glslangValidator NOT FOUND, please install Vulkan SDK properly.")
endif ()

set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(GLOB_RECURSE GLSL_SOURCE_FILES CONFIGURE_DEPENDS
        "${SHADER_SRC_DIR}/*.vert"
        "${SHADER_SRC_DIR}/*.frag"
        "${SHADER_SRC_DIR}/*.comp"
)
set(SPIRV_BINARY_FILES)
foreach(GLSL ${GLSL_SOURCE_FILES})
    file(RELATIVE_PATH REL ${SHADER_SRC_DIR} ${GLSL})
    set(SPIRV "${SHADER_OUT_DIR}/${REL}.spv")
    get_filename_component(SPIRV_DIR ${SPIRV} DIRECTORY)

    add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SPIRV_DIR}
            COMMAND ${GLSL_VALIDATOR} -V -I${SHADER_SRC_DIR} ${GLSL} -o ${SPIRV}
            DEPENDS ${GLSL}
            COMMENT "glslangValidator Compiling: ${REL} -> ${SPIRV}"
            VERBATIM)
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SPIRV_BINARY_FILES})
