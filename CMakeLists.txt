cmake_minimum_required(VERSION 3.16)
# Set the SDK floor before compiler detection, including melonDS and generated ASM.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    if(DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
        set(CMAKE_OSX_DEPLOYMENT_TARGET "$ENV{MACOSX_DEPLOYMENT_TARGET}" CACHE STRING "Minimum macOS version")
    else()
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    endif()
endif()
project(ssrecomp LANGUAGES C CXX)
set(SSR_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gen" CACHE PATH "Generated game sources")
set(SSR_GAMECODE_OPT "O1" CACHE STRING "Optimisation level for the generated game code")
set_property(CACHE SSR_GAMECODE_OPT PROPERTY STRINGS O1 O2 O3 Os)
if(NOT SSR_GAMECODE_OPT MATCHES "^(O1|O2|O3|Os)$")
    message(FATAL_ERROR "SSR_GAMECODE_OPT must be O1, O2, O3 or Os")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-O3 -fomit-frame-pointer)

# melonDS core is the hardware runtime; its own frontend, JIT and GL renderer are not used.
set(BUILD_QT_SDL OFF CACHE BOOL "" FORCE)
set(ENABLE_OGLRENDERER OFF CACHE BOOL "" FORCE)
set(ENABLE_JIT OFF CACHE BOOL "" FORCE)
set(ENABLE_GDBSTUB OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO_RELEASE OFF CACHE BOOL "" FORCE)
add_subdirectory(melonDS EXCLUDE_FROM_ALL)

option(SSR_MACOS_SOURCE_DEPS "Build SDL2 and libpng for the selected macOS target" ${APPLE})
if(APPLE AND NOT ANDROID AND SSR_MACOS_SOURCE_DEPS)
    include(cmake/MacDependencies.cmake)
else()
    find_package(SDL2 REQUIRED)
    if(NOT ANDROID)
        find_package(PNG REQUIRED)
    endif()
endif()

enable_language(ASM)

# Recompiled game code, produced by tools/recomp.py.
include("${SSR_GENERATED_DIR}/sources.cmake")
add_library(gamecode OBJECT ${RC_GENERATED_SOURCES})
target_include_directories(gamecode PRIVATE melonDS/src runtime "${SSR_GENERATED_DIR}")
# Generated code is huge and uniform; -O1 keeps build times reasonable. Other
# levels are available for measured comparisons, not assumed to be faster.
target_compile_options(gamecode PRIVATE -${SSR_GAMECODE_OPT} -g0 -fno-exceptions -fno-rtti "$<$<COMPILE_LANGUAGE:CXX>:-fno-stack-protector>"
    "$<$<COMPILE_LANGUAGE:ASM>:-Wa,-I${SSR_GENERATED_DIR}>")

set(SSRECOMP_SOURCES
    frontend/main.cpp
    frontend/platform.cpp
    runtime/recomp_rt.cpp
    runtime/recomp_mem.cpp
    "${SSR_GENERATED_DIR}/apps_table.cpp"
    $<TARGET_OBJECTS:gamecode>)
if(ANDROID)
    # On Android the game is the library libmain.so that SDL's Java activity loads
    # (tools/android/apk.py packs it into an APK). SDL2 is one built for Android
    # (CMAKE_PREFIX_PATH); no libpng: screenshots are a desktop test aid.
    add_library(ssrecomp SHARED ${SSRECOMP_SOURCES})
    set_target_properties(ssrecomp PROPERTIES OUTPUT_NAME main)
    target_include_directories(ssrecomp PRIVATE melonDS/src frontend runtime)
    # The app's version, for its check for a newer release: the newest one in CHANGELOG.md.
    set(SSR_VERSION "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS CHANGELOG.md)
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md" SSR_RELEASES REGEX "^## v[0-9]")
        if(SSR_RELEASES)
            list(GET SSR_RELEASES 0 SSR_VERSION)
            string(REGEX REPLACE "^## v([0-9.]+).*" "\\1" SSR_VERSION "${SSR_VERSION}")
        endif()
    endif()
    target_compile_definitions(ssrecomp PRIVATE SSR_NO_PNG SSR_VERSION="${SSR_VERSION}")
    target_link_libraries(ssrecomp PRIVATE core SDL2::SDL2 android log)
else()
    add_executable(ssrecomp ${SSRECOMP_SOURCES})
    target_include_directories(ssrecomp PRIVATE melonDS/src frontend runtime ${SDL2_INCLUDE_DIRS})
    find_package(Threads REQUIRED)
    if(TARGET SDL2::SDL2)
        if(TARGET SDL2::SDL2main)
            target_link_libraries(ssrecomp PRIVATE SDL2::SDL2main)
        endif()
        target_link_libraries(ssrecomp PRIVATE SDL2::SDL2)
    else()
        target_link_libraries(ssrecomp PRIVATE ${SDL2_LIBRARIES})
    endif()
    target_link_libraries(ssrecomp PRIVATE core PNG::PNG Threads::Threads)
endif()
