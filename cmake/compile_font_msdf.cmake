# ========================
# msdf-atlas-gen auto-download setup
# ========================

set(MSDF_VERSION "1.3")

set(MSDF_PRIMARY_URL
        "https://github.com/Chlumsky/msdf-atlas-gen/releases/download/v${MSDF_VERSION}/msdf-atlas-gen-${MSDF_VERSION}-win64.zip"
)

set(MSDF_BASE_DIR "${CMAKE_BINARY_DIR}/_deps/msdf-atlas-gen")
set(MSDF_ARCHIVE_PATH "${MSDF_BASE_DIR}/msdf-atlas-gen-${MSDF_VERSION}-win64.zip")
set(MSDF_EXTRACT_ROOT "${MSDF_BASE_DIR}")
set(MSDF_EXE_PATH "${MSDF_EXTRACT_ROOT}/msdf-atlas-gen/msdf-atlas-gen.exe")

if (NOT EXISTS "${MSDF_EXE_PATH}")
    file(MAKE_DIRECTORY "${MSDF_BASE_DIR}")

    message(STATUS "📥 Downloading msdf-atlas-gen v${MSDF_VERSION}")
    message(STATUS "    → from: ${MSDF_PRIMARY_URL}")

    set(CMAKE_TLS_VERIFY TRUE)
    file(DOWNLOAD
            "${MSDF_PRIMARY_URL}"
            "${MSDF_ARCHIVE_PATH}"
            SHOW_PROGRESS
            TLS_VERIFY ON
            STATUS _dl_status
    )

    list(GET _dl_status 0 _dl_code)
    if (NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "❌ Failed to download msdf-atlas-gen from ${MSDF_PRIMARY_URL}")
    endif ()

    message(STATUS "📦 Extracting archive to: ${MSDF_EXTRACT_ROOT}")
    file(ARCHIVE_EXTRACT INPUT "${MSDF_ARCHIVE_PATH}" DESTINATION "${MSDF_EXTRACT_ROOT}")

    if (EXISTS "${MSDF_ARCHIVE_PATH}")
        file(REMOVE "${MSDF_ARCHIVE_PATH}")
        message(STATUS "🧹 Removed archive: ${MSDF_ARCHIVE_PATH}")
    endif ()

endif ()

set(NOTO_MONTHLY_TAG "noto-monthly-release-2025.08.01")
set(NOTO_SANS_URL "https://github.com/notofonts/notofonts.github.io/raw/${NOTO_MONTHLY_TAG}/fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf")
set(FONTS_BASE_DIR "${CMAKE_BINARY_DIR}/_deps/fonts")
set(NOTO_SANS_TTF "${FONTS_BASE_DIR}/NotoSans-Regular.ttf")

if (NOT EXISTS "${NOTO_SANS_TTF}")
    file(MAKE_DIRECTORY "${FONTS_BASE_DIR}")

    message(STATUS "📥 Downloading test font: NotoSans-Regular.ttf")
    message(STATUS "    → from: ${NOTO_SANS_URL}")

    set(CMAKE_TLS_VERIFY TRUE)
    file(DOWNLOAD
            "${NOTO_SANS_URL}"
            "${NOTO_SANS_TTF}"
            SHOW_PROGRESS
            TLS_VERIFY ON
            STATUS _dl_status
    )
    list(GET _dl_status 0 _dl_code)
    if (NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "❌ Failed to download Noto Sans from ${NOTO_SANS_URL}")
    endif ()
    message(STATUS "✅ Saved to: ${NOTO_SANS_TTF}")
endif ()

set(MSDF_OUT_DIR "${CMAKE_BINARY_DIR}/assets")
file(MAKE_DIRECTORY "${MSDF_OUT_DIR}")

set(MSDF_OUT_PNG "${MSDF_OUT_DIR}/atlas_digits.png")
set(MSDF_OUT_JSON "${MSDF_OUT_DIR}/atlas_digits.json")

set(MSDF_CHARSET_FILE "${FONTS_BASE_DIR}/charset_digits.txt")
if (NOT EXISTS "${MSDF_CHARSET_FILE}")
    file(WRITE "${MSDF_CHARSET_FILE}" "\"0123456789.-+()%/\"\n")
endif ()


set(MSDF_TYPE "msdf")   # 可选 msdf 或 mtsdf
set(MSDF_PXRANGE "8")      # 距离范围像素. 与渲染时解码保持一致
set(MSDF_SIZE "48")     # 每个字形的 em 像素尺寸
set(MSDF_DIM_ARGS "-square4")
add_custom_command(
        OUTPUT "${MSDF_OUT_PNG}" "${MSDF_OUT_JSON}"
        COMMAND "${MSDF_EXE_PATH}"
        -font "${NOTO_SANS_TTF}"
        -charset "${MSDF_CHARSET_FILE}"
        -type "${MSDF_TYPE}"
        -size "${MSDF_SIZE}"
        -pxrange "${MSDF_PXRANGE}"
        ${MSDF_DIM_ARGS}
        -yorigin "top"
        -imageout "${MSDF_OUT_PNG}"
        -json "${MSDF_OUT_JSON}"
        DEPENDS "${NOTO_SANS_TTF}" "${MSDF_CHARSET_FILE}"
        COMMENT "[msdf-ag] Generating MSDF atlas: ${MSDF_OUT_PNG} and ${MSDF_OUT_JSON}"
        VERBATIM
)
add_custom_target(msdf_digits_atlas ALL
        DEPENDS "${MSDF_OUT_PNG}" "${MSDF_OUT_JSON}"
)