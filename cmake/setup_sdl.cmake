include(FetchContent)
FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG origin/main
        OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(SDL3)
function(copy_sdl_dlls TARGET_NAME)
    if (WIN32)
        add_custom_command(TARGET ${TARGET_NAME}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_RUNTIME_DLLS:${TARGET_NAME}>
                $<TARGET_FILE_DIR:${TARGET_NAME}>
                COMMAND_EXPAND_LISTS
                COMMENT "Copying SDL3 and dependent DLLs to runtime directory"
        )
    endif ()
endfunction()