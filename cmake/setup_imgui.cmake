# ============================
# Dear ImGui auto-download setup
# ============================

set(IMGUI_VERSION "1.92.2b")

set(IMGUI_PRIMARY_URL
        "https://github.com/ocornut/imgui/archive/refs/tags/v${IMGUI_VERSION}.zip"
)

set(IMGUI_BASE_DIR "${CMAKE_BINARY_DIR}/_deps/imgui")
set(IMGUI_ARCHIVE_PATH "${IMGUI_BASE_DIR}/imgui-${IMGUI_VERSION}.zip")
set(IMGUI_EXTRACT_ROOT "${IMGUI_BASE_DIR}")
set(IMGUI_SRC_DIR "${IMGUI_EXTRACT_ROOT}/imgui-${IMGUI_VERSION}")

# Only download/extract if imgui not already present
if (NOT IS_DIRECTORY "${IMGUI_SRC_DIR}")

    file(MAKE_DIRECTORY "${IMGUI_BASE_DIR}")

    message(STATUS "📥 Downloading Dear ImGui ${IMGUI_VERSION}")
    message(STATUS "    → from: ${IMGUI_PRIMARY_URL}")

    set(CMAKE_TLS_VERIFY TRUE)
    file(DOWNLOAD
            "${IMGUI_PRIMARY_URL}"
            "${IMGUI_ARCHIVE_PATH}"
            SHOW_PROGRESS
            TLS_VERIFY ON
            STATUS _dl_status
    )

    list(GET _dl_status 0 _dl_code)
    if (NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "❌ Failed to download Dear ImGui from ${IMGUI_PRIMARY_URL}")
    endif ()

    message(STATUS "📦 Extracting Dear ImGui to: ${IMGUI_EXTRACT_ROOT}")
    file(ARCHIVE_EXTRACT INPUT "${IMGUI_ARCHIVE_PATH}" DESTINATION "${IMGUI_EXTRACT_ROOT}")

    if (EXISTS "${IMGUI_ARCHIVE_PATH}")
        file(REMOVE "${IMGUI_ARCHIVE_PATH}")
        message(STATUS "🧹 Removed archive: ${IMGUI_ARCHIVE_PATH}")
    endif ()

endif ()


set(IMGUI_CORE_SOURCES
        "${IMGUI_SRC_DIR}/imgui.cpp"
        "${IMGUI_SRC_DIR}/imgui.h"
        "${IMGUI_SRC_DIR}/imgui_demo.cpp"
        "${IMGUI_SRC_DIR}/imgui_draw.cpp"
        "${IMGUI_SRC_DIR}/imgui_widgets.cpp"
        "${IMGUI_SRC_DIR}/imgui_tables.cpp"
)
set(IMGUI_BACKEND_SOURCES
        "${IMGUI_SRC_DIR}/backends/imgui_impl_sdl3.cpp"
        "${IMGUI_SRC_DIR}/backends/imgui_impl_sdl3.h"
        "${IMGUI_SRC_DIR}/backends/imgui_impl_vulkan.cpp"
        "${IMGUI_SRC_DIR}/backends/imgui_impl_vulkan.h"
)
add_library(imgui STATIC
        ${IMGUI_CORE_SOURCES}
        ${IMGUI_BACKEND_SOURCES}
)
target_include_directories(imgui
        PUBLIC
        "${IMGUI_SRC_DIR}"
        "${IMGUI_SRC_DIR}/backends"
)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan SDL3::SDL3)