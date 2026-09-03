# Build configuration for the SimpleGraphicsSuite SKSE plugin.
# The project name drives the name of the .dll, the .ini and the .log file.

cmake_minimum_required(VERSION 3.21)

project(SimpleGraphicsSuite VERSION 1.0.0 LANGUAGES CXX)

set(PLUGIN_AUTHOR "RoseEden" CACHE STRING "Author name baked into the plugin declaration")

# Folder created under the mods folder. Separate from the project name so it can
# carry a space without renaming the files.
set(MOD_FOLDER_NAME "${PROJECT_NAME}" CACHE STRING "Folder name under SKYRIM_MODS_FOLDER")

# Output location, picked up from the environment so this file stays portable.
# Set SKYRIM_FOLDER to a Skyrim Special Edition install, or SKYRIM_MODS_FOLDER
# to the "mods" folder of Mod Organizer 2 or Vortex.
if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
endif()

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${MOD_FOLDER_NAME}")
endif()

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

# SKSE::Trampoline's write_branch only handles an existing call/jmp site; the
# Vanilla HDR shader hook patches a function's own entry point instead, which
# needs a hand-built Xbyak stub like doodlum's SSEShaderTools uses.
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)

# CommonLibSSE-NG is a git submodule pinned under extern/.
set(
    COMMONLIBSSE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG"
    CACHE PATH
    "Path to CommonLibSSE-NG"
)

add_subdirectory(
    "${COMMONLIBSSE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/commonlibsse-ng"
)

include("${COMMONLIBSSE_DIR}/cmake/CommonLibSSE.cmake")

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR ${PLUGIN_AUTHOR}
    SOURCES
        src/plugin.cpp
        src/Compatibility.cpp
        src/Config.cpp
        src/DeviceHook.cpp
        src/FrameBufferCache.cpp
        src/Logging.cpp
        src/NativeMenuIntegration.cpp
        src/PresentHook.cpp
        src/RenderPass.cpp
        src/Streamline.cpp
        src/Features/Accessibility.cpp
        src/Features/AntiAliasing.cpp
        src/Features/Debug.cpp
        src/Features/LUT.cpp
        src/Features/Reflex.cpp
        src/Features/PostProcessing.cpp
        src/Features/Upscaling.cpp
        src/Features/DLSS.cpp
)

# include/ holds NativeSystemMenuFramework.h, header-only and resolved through
# GetProcAddress, so nothing to link there. extern/nvapi is the NVIDIA NVAPI
# SDK: headers at its root, plus the prebuilt import lib below.
# extern/Streamline is header + prebuilt-DLL only (loaded via GetProcAddress,
# same as NativeSystemMenuFramework.h) - DLSS itself is reached through it instead of
# linking the raw NGX SDK directly.
target_include_directories(${PROJECT_NAME} PRIVATE src include extern/nvapi extern/Streamline/include)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_link_libraries(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/nvapi/amd64/nvapi64.lib"
    d3dcompiler.lib
)

# Symbols in every configuration, so a crash log names a function instead of an
# offset. /Z7 keeps the debug info in the object files; /DEBUG writes the .pdb
# and switches /OPT off, so REF goes back in. ICF stays off, since folded
# functions resolve to the wrong name.
target_compile_options(${PROJECT_NAME} PRIVATE /Z7)
target_link_options(${PROJECT_NAME} PRIVATE /DEBUG /OPT:REF /OPT:NOICF)

# Deploy the plugin, its settings and its shaders after every build.
if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")

    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    # Crash Logger SSE reads a .pdb sitting next to the plugin.
    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    # Only deployed if missing - never overwrites settings already tweaked
    # in game, in dev or on a player's install.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/package/${PROJECT_NAME}.ini")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}/package/${PROJECT_NAME}.ini"
                "-DDST=${DLL_FOLDER}/${PROJECT_NAME}.ini"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyIfMissing.cmake"
            VERBATIM
        )
    endif()

    # Translation files, read by NativeSystemMenuFramework rather than the engine -
    # which is what lets a DLL-only mod be translated without a dummy esp.
    # Overwritten every build, unlike the ini: these are content, not settings
    # a player edits.
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Interface/Translations")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/Interface/Translations"
                "${OUTPUT_FOLDER}/Interface/Translations"
            VERBATIM
        )
    endif()

    # Vanilla HDR shaders, loaded at runtime from Data/Shaders/<technique>/.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Data/Shaders")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Data/Shaders" "${OUTPUT_FOLDER}/Shaders"
            VERBATIM
        )
    endif()

    # Vendor DLLs (Streamline's interposer and plugins) - same layout
    # Community Shaders uses for these binaries.
    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_FOLDER}/Shaders/${PROJECT_NAME}/Streamline"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/extern/Streamline/bin/x64"
            "${OUTPUT_FOLDER}/Shaders/${PROJECT_NAME}/Streamline"
        VERBATIM
    )
endif()
