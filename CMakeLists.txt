cmake_minimum_required(VERSION 3.16)
# Set the SDK floor before compiler detection, including melonDS and generated ASM.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    if(DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
        set(CMAKE_OSX_DEPLOYMENT_TARGET "$ENV{MACOSX_DEPLOYMENT_TARGET}" CACHE STRING "Minimum macOS version")
    else()
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    endif()
endif()
if(POLICY CMP0141)
    # Debug information is chosen per target (MSVC_DEBUG_INFORMATION_FORMAT), so the
    # generated game code can go without it on clang-cl as it does with -g0 elsewhere.
    cmake_policy(SET CMP0141 NEW)
endif()
if(SSR_UWP)
    # Only the system libraries a UWP program may use (WindowsApp.lib covers them all), and
    # the CRT of the Microsoft.VCLibs framework package: the build runs in a "vcvarsall x64
    # uwp" environment, whose LIB points at the store CRT. Set before project() so that
    # CMake's own compiler checks link the same way (that CRT needs WindowsApp.lib).
    set(CMAKE_C_STANDARD_LIBRARIES "WindowsApp.lib" CACHE STRING "")
    set(CMAKE_CXX_STANDARD_LIBRARIES "WindowsApp.lib" CACHE STRING "")
    set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CMAKE_C_STANDARD_LIBRARIES CMAKE_CXX_STANDARD_LIBRARIES)
endif()
project(ssrecomp LANGUAGES C CXX)
set(SSR_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gen" CACHE PATH "Generated game sources")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The Xbox (and Windows Store style) app: a UWP program, packed into an MSIX by the setup
# program (tools/xbox/xbox.py). Built with clang-cl against an SDL2 made for UWP; the
# Xbox only starts UWP programs, not ordinary Windows ones.
option(SSR_UWP "Build the UWP app for Xbox (clang-cl; packed by the setup program)" OFF)
# The Xbox launcher (tools/xbox/launcher.py): built without the game's code, it runs the
# game on melonDS's interpreter, so it needs no ROM to build and holds nothing of the game.
option(SSR_LAUNCHER "Run the game on the interpreter by default (for a build without the game's code)" OFF)
if(SSR_UWP AND NOT MSVC)
    message(FATAL_ERROR "SSR_UWP needs clang-cl (an MSVC-style compiler)")
endif()
if(MSVC)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "Of the MSVC-style compilers only clang-cl is supported (or build with MinGW GCC)")
    endif()
    # clang-cl's RelWithDebInfo inlines only functions marked inline (/Ob1), which costs
    # the melonDS core a lot of speed; GCC and Clang inline freely at -O2.
    foreach(lang C CXX)
        string(REPLACE "/Ob1" "/Ob2" CMAKE_${lang}_FLAGS_RELWITHDEBINFO "${CMAKE_${lang}_FLAGS_RELWITHDEBINFO}")
    endforeach()
    # As the melonDS core is built (its definitions only reach its own directory): its
    # headers use std::min and std::max, which <windows.h> would otherwise replace.
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
    # The core's FATStorage.cpp includes <dirent.h> without using it; the Windows SDK has none.
    set(DIRENT_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/msvc" CACHE PATH "" FORCE)
endif()
if(SSR_UWP)
    # Xbox One's processor has no AVX2.
    set(SSR_PORTABLE_X86 ON CACHE BOOL "" FORCE)
    # Code for that processor (AMD Jaguar: AVX, SSE4.2, BMI1, F16C, and its own instruction
    # scheduling), which the Xbox Series processors also run.
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/clang:-march=btver2>")
endif()

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
    if(NOT ANDROID AND NOT SSR_UWP)
        # Screenshots for testing; a clang-cl build without libpng goes without them.
        if(MSVC)
            find_package(PNG)
        else()
            find_package(PNG REQUIRED)
        endif()
    endif()
endif()

enable_language(ASM)

# Recompiled game code, produced by tools/recomp.py.
include("${SSR_GENERATED_DIR}/sources.cmake")
add_library(gamecode OBJECT ${RC_GENERATED_SOURCES})
target_include_directories(gamecode PRIVATE melonDS/src runtime "${SSR_GENERATED_DIR}")
# Generated code is huge and uniform; -O1 keeps build times reasonable.
if(MSVC)
    # The same options for clang-cl. Its /O1 means "small" (-Os), so -O1 is passed through
    # as it is; it comes after the configuration's /O2 and wins.
    target_compile_options(gamecode PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/clang:-O1;/EHs-c-;/GR-;/GS->"
        "$<$<COMPILE_LANGUAGE:ASM>:/clang:-Wa,-I${SSR_GENERATED_DIR}>")
    set_target_properties(gamecode PROPERTIES MSVC_DEBUG_INFORMATION_FORMAT "")
else()
    target_compile_options(gamecode PRIVATE -O1 -g0 -fno-exceptions -fno-rtti "$<$<COMPILE_LANGUAGE:CXX>:-fno-stack-protector>"
        "$<$<COMPILE_LANGUAGE:ASM>:-Wa,-I${SSR_GENERATED_DIR}>")
endif()

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
    target_compile_definitions(ssrecomp PRIVATE SSR_NO_PNG SSR_VERSION="${SSR_VERSION}" $<$<BOOL:${SSR_LAUNCHER}>:SSR_LAUNCHER>)
    target_link_libraries(ssrecomp PRIVATE core SDL2::SDL2 android log)
else()
    add_executable(ssrecomp ${SSRECOMP_SOURCES})
    target_include_directories(ssrecomp PRIVATE melonDS/src frontend runtime ${SDL2_INCLUDE_DIRS})
    find_package(Threads REQUIRED)
    if(TARGET SDL2::SDL2)
        if(TARGET SDL2::SDL2main AND NOT SSR_UWP) # (main.cpp has its own UWP entry point)
            target_link_libraries(ssrecomp PRIVATE SDL2::SDL2main)
        endif()
        target_link_libraries(ssrecomp PRIVATE SDL2::SDL2)
    else()
        target_link_libraries(ssrecomp PRIVATE ${SDL2_LIBRARIES})
    endif()
    target_link_libraries(ssrecomp PRIVATE core Threads::Threads)
    if(TARGET PNG::PNG)
        target_link_libraries(ssrecomp PRIVATE PNG::PNG)
    else()
        target_compile_definitions(ssrecomp PRIVATE SSR_NO_PNG)
    endif()
    if(SSR_UWP)
        # The app's own entry point and paths in main.cpp. Not WINAPI_FAMILY_APP: the C++
        # library no longer compiles with it; linking WindowsApp.lib alone is what keeps the
        # program to the system functions a UWP program may use.
        target_compile_definitions(ssrecomp PRIVATE SSR_UWP $<$<BOOL:${SSR_LAUNCHER}>:SSR_LAUNCHER>)
        set_target_properties(ssrecomp PROPERTIES WIN32_EXECUTABLE ON)
        target_link_options(ssrecomp PRIVATE /APPCONTAINER)
    endif()
endif()
