# ==========================
# stb_image auto-download setup
# ==========================

set(STB_REF "master")

set(STB_PRIMARY_URL
        "https://github.com/nothings/stb/archive/refs/heads/${STB_REF}.zip"
)


set(STB_BASE_DIR "${CMAKE_BINARY_DIR}/_deps/stb")
set(STB_ARCHIVE_PATH "${STB_BASE_DIR}/stb-${STB_REF}.zip")
set(STB_EXTRACT_ROOT "${STB_BASE_DIR}")
set(STB_SRC_DIR "${STB_EXTRACT_ROOT}/stb-${STB_REF}")
set(STB_INCLUDE_DIR "${STB_SRC_DIR}")

# Only download/extract if stb not already present
if (NOT IS_DIRECTORY "${STB_SRC_DIR}")

    file(MAKE_DIRECTORY "${STB_BASE_DIR}")

    message(STATUS "📥 Downloading stb (${STB_REF})")
    message(STATUS "    → from: ${STB_PRIMARY_URL}")

    set(CMAKE_TLS_VERIFY TRUE)
    file(DOWNLOAD
            "${STB_PRIMARY_URL}"
            "${STB_ARCHIVE_PATH}"
            SHOW_PROGRESS
            TLS_VERIFY ON
            STATUS _dl_status
    )

    list(GET _dl_status 0 _dl_code)
    if (NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "❌ Failed to download stb from ${STB_PRIMARY_URL}")
    endif ()

    message(STATUS "📦 Extracting stb to: ${STB_EXTRACT_ROOT}")
    file(ARCHIVE_EXTRACT INPUT "${STB_ARCHIVE_PATH}" DESTINATION "${STB_EXTRACT_ROOT}")

    if (EXISTS "${STB_ARCHIVE_PATH}")
        file(REMOVE "${STB_ARCHIVE_PATH}")
        message(STATUS "🧹 Removed archive: ${STB_ARCHIVE_PATH}")
    endif ()

endif ()

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE
        "$<BUILD_INTERFACE:${STB_INCLUDE_DIR}>"
)

message(STATUS "✅ stb_image (${STB_REF}) is ready!")