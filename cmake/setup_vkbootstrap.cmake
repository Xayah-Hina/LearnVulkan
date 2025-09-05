# ================================
# vk-bootstrap auto-download setup
# ================================

set(VKB_VERSION "1.4.321")

set(VKB_PRIMARY_URL
        "https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v${VKB_VERSION}.zip"
)

set(VKB_BASE_DIR "${CMAKE_BINARY_DIR}/_deps/vk-bootstrap")
set(VKB_ARCHIVE_PATH "${VKB_BASE_DIR}/vk-bootstrap-${VKB_VERSION}.zip")
set(VKB_EXTRACT_ROOT "${VKB_BASE_DIR}")
set(VKB_SRC_DIR "${VKB_EXTRACT_ROOT}/vk-bootstrap-${VKB_VERSION}")

# Only download/extract if vk-bootstrap not already present
if (NOT IS_DIRECTORY "${VKB_SRC_DIR}")

    file(MAKE_DIRECTORY "${VKB_BASE_DIR}")

    message(STATUS "📥 Downloading vk-bootstrap ${VKB_VERSION}")
    message(STATUS "    → from: ${VKB_PRIMARY_URL}")

    set(CMAKE_TLS_VERIFY TRUE)
    file(DOWNLOAD
            "${VKB_PRIMARY_URL}"
            "${VKB_ARCHIVE_PATH}"
            SHOW_PROGRESS
            TLS_VERIFY ON
            STATUS _dl_status
    )

    list(GET _dl_status 0 _dl_code)
    if (NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "❌ Failed to download vk-bootstrap from ${VKB_PRIMARY_URL}")
    endif ()

    message(STATUS "📦 Extracting vk-bootstrap to: ${VKB_EXTRACT_ROOT}")
    file(ARCHIVE_EXTRACT INPUT "${VKB_ARCHIVE_PATH}" DESTINATION "${VKB_EXTRACT_ROOT}")

    if (EXISTS "${VKB_ARCHIVE_PATH}")
        file(REMOVE "${VKB_ARCHIVE_PATH}")
        message(STATUS "🧹 Removed archive: ${VKB_ARCHIVE_PATH}")
    endif ()

endif ()

# =============
# Attach helper
# =============
# usage: vkb_attach_to_target(MyApp)
function(vkb_attach_to_target target)
    if (NOT TARGET ${target})
        message(FATAL_ERROR "Target '${target}' does not exist")
    endif ()

    set(_VKB_HDR "${VKB_SRC_DIR}/src/VkBootstrap.h")
    set(_VKB_HDR_DISP "${VKB_SRC_DIR}/src/VkBootstrapDispatch.h")
    set(_VKB_CPP "${VKB_SRC_DIR}/src/VkBootstrap.cpp")

    foreach (f IN LISTS _VKB_HDR _VKB_HDR_DISP _VKB_CPP)
        if (NOT EXISTS "${f}")
            message(FATAL_ERROR "vk-bootstrap file missing: ${f}")
        endif ()
    endforeach ()

    target_sources(${target} PRIVATE "${_VKB_CPP}")
    target_include_directories(${target} PRIVATE "${VKB_SRC_DIR}/src")

    # suppress warnings C4819
    target_compile_options(${target} PRIVATE
            $<$<OR:$<CXX_COMPILER_ID:MSVC>,$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>:/utf-8>
    )

    if (UNIX)
        target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS})
    endif ()

    message(STATUS "✅ vk-bootstrap ${VKB_VERSION} attached to '${target}' (copy-paste mode)")
endfunction()

message(STATUS "VKB_SRC_DIR" "${VKB_SRC_DIR}")