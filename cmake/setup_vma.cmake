# ================================
# VMA auto-download and setup
# ================================

set(VMA_VERSION "3.3.0")

set(VMA_PRIMARY_URL
        "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v${VMA_VERSION}.zip"
)

set(VMA_BASE_DIR "${CMAKE_BINARY_DIR}/_deps/vma")
set(VMA_ARCHIVE_PATH "${VMA_BASE_DIR}/VulkanMemoryAllocator-${VMA_VERSION}.zip")
set(VMA_EXTRACT_ROOT "${VMA_BASE_DIR}")
set(VMA_SRC_DIR "${VMA_EXTRACT_ROOT}/VulkanMemoryAllocator-${VMA_VERSION}")

# Only download/extract if VMA not already present
if (NOT IS_DIRECTORY "${VMA_SRC_DIR}")

    file(MAKE_DIRECTORY "${VMA_BASE_DIR}")

    message(STATUS "📥 Downloading VMA ${VKB_VERSION}")
    message(STATUS "    → from: ${VMA_PRIMARY_URL}")

    set(CMAKE_TLS_VERIFY TRUE)
    file(DOWNLOAD
            "${VMA_PRIMARY_URL}"
            "${VMA_ARCHIVE_PATH}"
            SHOW_PROGRESS
            TLS_VERIFY ON
            STATUS _dl_status
    )

    list(GET _dl_status 0 _dl_code)
    if (NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "❌ Failed to download VMA from ${VMA_PRIMARY_URL}")
    endif ()

    message(STATUS "📦 Extracting VMA to: ${VMA_EXTRACT_ROOT}")
    file(ARCHIVE_EXTRACT INPUT "${VMA_ARCHIVE_PATH}" DESTINATION "${VMA_EXTRACT_ROOT}")

    if (EXISTS "${VMA_ARCHIVE_PATH}")
        file(REMOVE "${VMA_ARCHIVE_PATH}")
        message(STATUS "🧹 Removed archive: ${VMA_ARCHIVE_PATH}")
    endif ()

endif ()

set(VMA_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(VMA_BUILD_SAMPLE_SHADERS OFF CACHE BOOL "" FORCE)
set(VMA_BUILD_REPLAY OFF CACHE BOOL "" FORCE)

add_subdirectory("${VMA_SRC_DIR}" "${VMA_BASE_DIR}/build" EXCLUDE_FROM_ALL)
