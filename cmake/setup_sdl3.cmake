# ========================
# SDL3 auto-download setup
# ========================

set(SDL3_VERSION "3.2.20")

set(SDL3_PRIMARY_URL
        "https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}/SDL3-devel-${SDL3_VERSION}-VC.zip"
)

set(SDL3_BASE_DIR "${CMAKE_BINARY_DIR}/_deps/sdl3")
set(SDL3_ARCHIVE_PATH "${SDL3_BASE_DIR}/SDL3-devel-${SDL3_VERSION}-VC.zip")
set(SDL3_EXTRACT_ROOT "${SDL3_BASE_DIR}")
set(SDL3_DIR "${SDL3_EXTRACT_ROOT}/SDL3-${SDL3_VERSION}/cmake")

# Only download/extract if SDL3 not already present
if (NOT IS_DIRECTORY "${SDL3_DIR}")

    file(MAKE_DIRECTORY "${SDL3_BASE_DIR}")

    message(STATUS "📥 Downloading SDL3 v${SDL3_VERSION}")
    message(STATUS "    → from: ${SDL3_PRIMARY_URL}")

    set(CMAKE_TLS_VERIFY TRUE)
    file(DOWNLOAD
            "${SDL3_PRIMARY_URL}"
            "${SDL3_ARCHIVE_PATH}"
            SHOW_PROGRESS
            TLS_VERIFY ON
            STATUS _dl_status
    )

    list(GET _dl_status 0 _dl_code)
    if (NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "❌ Failed to download SDL3 from ${SDL3_PRIMARY_URL}")
    endif ()

    message(STATUS "📦 Extracting archive to: ${SDL3_EXTRACT_ROOT}")
    file(ARCHIVE_EXTRACT INPUT "${SDL3_ARCHIVE_PATH}" DESTINATION "${SDL3_EXTRACT_ROOT}")

    if (EXISTS "${SDL3_ARCHIVE_PATH}")
        file(REMOVE "${SDL3_ARCHIVE_PATH}")
        message(STATUS "🧹 Removed archive: ${SDL3_ARCHIVE_PATH}")
    endif ()

endif ()

# Find the package after extraction
find_package(SDL3 CONFIG REQUIRED)

message(STATUS "✅ SDL3 v${SDL3_VERSION} is ready!")