# ========================
# GLM auto-download setup
# ========================

set(GLM_VERSION "1.0.1")

set(GLM_PRIMARY_URL
        "https://github.com/g-truc/glm/archive/refs/tags/${GLM_VERSION}.zip"
)

set(GLM_BASE_DIR "${CMAKE_BINARY_DIR}/_deps/glm")
set(GLM_ARCHIVE_PATH "${GLM_BASE_DIR}/glm-${GLM_VERSION}.zip")
set(GLM_EXTRACT_ROOT "${GLM_BASE_DIR}")
set(glm_DIR "${GLM_EXTRACT_ROOT}/glm-${GLM_VERSION}/cmake")

# Only download/extract if glm not already present
if (NOT IS_DIRECTORY "${glm_DIR}")

    file(MAKE_DIRECTORY "${GLM_BASE_DIR}")

    message(STATUS "📥 Downloading GLM v${GLM_VERSION}")
    message(STATUS "    → from: ${GLM_PRIMARY_URL}")

    set(CMAKE_TLS_VERIFY TRUE)
    file(DOWNLOAD
            "${GLM_PRIMARY_URL}"
            "${GLM_ARCHIVE_PATH}"
            SHOW_PROGRESS
            TLS_VERIFY ON
            STATUS _dl_status
    )

    list(GET _dl_status 0 _dl_code)
    if (NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "❌ Failed to download GLM from ${GLM_PRIMARY_URL}")
    endif ()

    message(STATUS "📦 Extracting GLM to: ${GLM_EXTRACT_ROOT}")
    file(ARCHIVE_EXTRACT INPUT "${GLM_ARCHIVE_PATH}" DESTINATION "${GLM_EXTRACT_ROOT}")

    if (EXISTS "${GLM_ARCHIVE_PATH}")
        file(REMOVE "${GLM_ARCHIVE_PATH}")
        message(STATUS "🧹 Removed archive: ${GLM_ARCHIVE_PATH}")
    endif ()

endif ()

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE
        "$<BUILD_INTERFACE:${GLM_INCLUDE_DIR}>"
)

message(STATUS "✅ GLM v${GLM_VERSION} is ready!")