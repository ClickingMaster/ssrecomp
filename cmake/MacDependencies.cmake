# Build Mac dependencies for the same architecture and deployment target as the game.
# Prebuilt Homebrew libraries can require a newer macOS than the selected target.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
# Populate iOS sources first so the backend can be patched before SDL configures.
set(_ssr_sdl_subdir ".")
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_ssr_sdl_subdir "fulldex-populate-only")
endif()
FetchContent_Declare(ssr_sdl2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
        https://www.libsdl.org/release/SDL2-2.32.10.tar.gz
    SOURCE_SUBDIR "${_ssr_sdl_subdir}"
    TIMEOUT 90
    TLS_VERIFY TRUE
    URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165)
# Patch before add_subdirectory, including already populated dependency caches.
FetchContent_MakeAvailable(ssr_sdl2)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    find_package(Git REQUIRED)
    # A tarball inside our checkout has no .git of its own. Stop Git finding
    # the outer repository, where apply would silently skip these relative paths.
    get_filename_component(_sdl_parent "${ssr_sdl2_SOURCE_DIR}" DIRECTORY)
    set(_scene_git "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_sdl_parent}" "${GIT_EXECUTABLE}")
    set(_scene_patch "${CMAKE_CURRENT_LIST_DIR}/../tools/ios/sdl2-scene-lifecycle.patch")
    execute_process(COMMAND ${_scene_git} apply --reverse --check "${_scene_patch}"
        WORKING_DIRECTORY "${ssr_sdl2_SOURCE_DIR}" RESULT_VARIABLE _scene_applied
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT _scene_applied EQUAL 0)
        # Upgrade the exact previous Fulldex patch in existing Mac build caches.
        # Only reverse a recognised patch; never overwrite arbitrary local edits.
        set(_scene_v1 "${CMAKE_CURRENT_LIST_DIR}/../tools/ios/sdl2-scene-lifecycle-v1.patch")
        execute_process(COMMAND ${_scene_git} apply --reverse --check "${_scene_v1}"
            WORKING_DIRECTORY "${ssr_sdl2_SOURCE_DIR}" RESULT_VARIABLE _scene_legacy
            OUTPUT_QUIET ERROR_QUIET)
        if(_scene_legacy EQUAL 0)
            execute_process(COMMAND ${_scene_git} apply --reverse "${_scene_v1}"
                WORKING_DIRECTORY "${ssr_sdl2_SOURCE_DIR}" RESULT_VARIABLE _scene_unpatch)
            if(NOT _scene_unpatch EQUAL 0)
                message(FATAL_ERROR "Could not upgrade the previous SDL2 scene patch")
            endif()
        endif()
        execute_process(COMMAND ${_scene_git} apply --check "${_scene_patch}"
            WORKING_DIRECTORY "${ssr_sdl2_SOURCE_DIR}" RESULT_VARIABLE _scene_check)
        if(NOT _scene_check EQUAL 0)
            message(FATAL_ERROR "SDL2 scene patch does not match; remove the cached SDL2 source and configure again")
        endif()
        execute_process(COMMAND ${_scene_git} apply "${_scene_patch}"
            WORKING_DIRECTORY "${ssr_sdl2_SOURCE_DIR}" RESULT_VARIABLE _scene_result)
        if(NOT _scene_result EQUAL 0)
            message(FATAL_ERROR "Could not apply SDL2 scene lifecycle support")
        endif()
    endif()
    add_subdirectory("${ssr_sdl2_SOURCE_DIR}" "${ssr_sdl2_BINARY_DIR}")
endif()

# (iOS, tools/ios/build_all.sh: SDL2 only. libpng is for the test harness's screenshots.)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    return()
endif()

# zlib is supplied by macOS; use the SDK copy, never a newer Homebrew dylib.
set(SSR_MACOS_SDK "${CMAKE_OSX_SYSROOT}")
if(NOT IS_DIRECTORY "${SSR_MACOS_SDK}")
    if(NOT SSR_MACOS_SDK)
        set(SSR_MACOS_SDK macosx)
    endif()
    execute_process(COMMAND xcrun --sdk "${SSR_MACOS_SDK}" --show-sdk-path
        OUTPUT_VARIABLE SSR_MACOS_SDK OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE SSR_SDK_RESULT)
    if(NOT SSR_SDK_RESULT EQUAL 0)
        message(FATAL_ERROR "Install/select Xcode Command Line Tools before building")
    endif()
endif()
find_library(SSR_SDK_ZLIB NAMES z PATHS "${SSR_MACOS_SDK}/usr/lib" NO_DEFAULT_PATH REQUIRED)
set(ZLIB_LIBRARY "${SSR_SDK_ZLIB}" CACHE FILEPATH "SDK zlib" FORCE)
set(ZLIB_INCLUDE_DIR "${SSR_MACOS_SDK}/usr/include" CACHE PATH "SDK zlib headers" FORCE)
set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ssr_png
    URL https://download.sourceforge.net/libpng/libpng-1.6.59.tar.gz
    TIMEOUT 90
    TLS_VERIFY TRUE
    URL_HASH SHA256=86a3e4b501f7f50e392c4e456ea158893e9a595b1c63eb88c7bb9f6cf8772dad)
FetchContent_MakeAvailable(ssr_png)
add_library(PNG::PNG ALIAS png_static)
