# The player program, Pokemon GBA Widescreen (frontend/), on mGBA with this project's wide
# renderer. setup.sh makes what it needs: the patched mGBA source and the widescreen patches.
#
#   cmake -S . -B build-player -G Ninja -DCMAKE_BUILD_TYPE=Release
#   ninja -C build-player
#
# tools/pc/build.sh makes the downloads for Windows and Linux.
#
# Built into another program (the all-in-one launcher of Pokemon DS Widescreen): that program's
# CMakeLists sets GBAW_EMBEDDED and adds this folder; it gets the library gbawide, whose
# frontend/embed.h lists the GBA games and plays one. SDL2 is that program's.
cmake_minimum_required(VERSION 3.16)
project(pokemon_gba_wide LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(GBAW_WORK "$ENV{HOME}/pokemon-gba-wide" CACHE PATH "setup.sh's work folder")
set(GBAW_MGBA "${GBAW_WORK}/mgba" CACHE PATH "mGBA with patches/mgba-wide.patch applied")
set(GBAW_PATCHES "${GBAW_WORK}/build/patches" CACHE PATH "The widescreen patches (emerald-wide.ips and so on)")
# (Or the header tools/embed_patches.py makes from them, made beforehand.)
set(GBAW_PATCHES_H "" CACHE FILEPATH "patches.h made by tools/embed_patches.py, instead of making it here")
set(GBAW_PATCH_FILES emerald-wide.ips emerald-wide.safety firered-wide.ips firered-wide.safety
    firered11-wide.ips firered11-wide.safety leafgreen-wide.ips leafgreen-wide.safety leafgreen11-wide.ips leafgreen11-wide.safety
    ruby-wide.ips ruby-wide.safety ruby11-wide.ips ruby11-wide.safety ruby10-wide.ips ruby10-wide.safety
    sapphire-wide.ips sapphire-wide.safety sapphire11-wide.ips sapphire11-wide.safety sapphire10-wide.ips sapphire10-wide.safety
    rubyde-wide.ips rubyde-wide.safety sapphirede-wide.ips sapphirede-wide.safety
    rubyfr-wide.ips rubyfr-wide.safety rubyes-wide.ips rubyes-wide.safety rubyit-wide.ips rubyit-wide.safety
    rubyjp-wide.ips rubyjp-wide.safety sapphirefr-wide.ips sapphirefr-wide.safety sapphirees-wide.ips sapphirees-wide.safety
    sapphireit-wide.ips sapphireit-wide.safety sapphirejp-wide.ips sapphirejp-wide.safety)
if(NOT GBAW_PATCHES_H)
    foreach(f ${GBAW_PATCH_FILES})
        if(NOT EXISTS "${GBAW_PATCHES}/${f}")
            message(FATAL_ERROR "${GBAW_PATCHES}/${f} is missing: run setup.sh first")
        endif()
        list(APPEND GBAW_PATCH_DEPS "${GBAW_PATCHES}/${f}")
    endforeach()
endif()
if(NOT EXISTS "${GBAW_MGBA}/include/mgba/internal/gba/video.h")
    message(FATAL_ERROR "No mGBA source at ${GBAW_MGBA}: run setup.sh first")
endif()

# mGBA: only the GBA core and what it needs to run a game; no frontends, scripting, debuggers
# or extra libraries (so the Windows build needs nothing but SDL2).
foreach(opt BUILD_QT BUILD_SDL BUILD_LIBRETRO BUILD_GL BUILD_GLES2 BUILD_GLES3 BUILD_SHARED BUILD_PYTHON
            BUILD_TEST BUILD_SUITE BUILD_CINEMA BUILD_EXAMPLE BUILD_PERF BUILD_UPDATER BUILD_DOCGEN BUILD_HEADLESS BUILD_ROM_TEST
            USE_FFMPEG USE_LUA USE_SQLITE3 USE_ELF USE_EPOXY USE_DISCORD_RPC USE_LIBZIP USE_MINIZIP USE_ZLIB USE_PNG
            USE_FREETYPE USE_LZMA USE_EDITLINE USE_JSON_C USE_GDB_STUB ENABLE_GDB_STUB ENABLE_DEBUGGERS ENABLE_SCRIPTING M_CORE_GB)
    set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(M_CORE_GBA ON CACHE BOOL "" FORCE)
set(SKIP_GIT ON CACHE BOOL "" FORCE)
# Nintendo Switch homebrew: built with devkitPro's toolchain file ($DEVKITPRO/cmake/Switch.cmake);
# mGBA has its own Switch settings (file access, libnx), switched on by SWITCH.
if(NINTENDO_SWITCH OR CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    set(GBAW_SWITCH ON)
    set(SWITCH ON)
    if(NOT DEVKITPRO)
        set(DEVKITPRO "$ENV{DEVKITPRO}")
    endif()
    if(NOT DEVKITPRO)
        set(DEVKITPRO /opt/devkitpro)
    endif()
else()
    set(GBAW_SWITCH OFF)
endif()
add_subdirectory("${GBAW_MGBA}" mgba EXCLUDE_FROM_ALL)
option(GBAW_EMBEDDED "Only the library gbawide, for another program (frontend/embed.h)" OFF)
if(GBAW_EMBEDDED)
    # (mGBA's own crc32, without zlib, by another name: the launcher has zlib's.)
    target_compile_definitions(mgba PRIVATE crc32=gbaw_crc32)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(GBAW_PATCHES_H)
    configure_file("${GBAW_PATCHES_H}" "${CMAKE_CURRENT_BINARY_DIR}/gen/patches.h" COPYONLY)
else()
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gen/patches.h"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/gen"
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_patches.py" "${GBAW_PATCHES}" "${CMAKE_CURRENT_BINARY_DIR}/gen/patches.h"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_patches.py" ${GBAW_PATCH_DEPS}
        VERBATIM)
endif()

if(GBAW_EMBEDDED AND (TARGET SDL2::SDL2 OR SDL2_STATIC_INCLUDE_DIRS OR SDL2_INCLUDE_DIRS))
    # (The launcher's SDL2, found already.)
elseif(GBAW_SWITCH)
    # SDL2 from devkitPro's portlibs (switch-sdl2), linked statically with what it needs, which
    # only its pkg-config file lists.
    if(NOT PKG_CONFIG_EXECUTABLE)
        find_program(PKG_CONFIG_EXECUTABLE NAMES aarch64-none-elf-pkg-config
            HINTS "${DEVKITPRO}/portlibs/switch/bin" NO_CMAKE_FIND_ROOT_PATH)
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
else()
    find_package(SDL2 REQUIRED)
endif()
# The games' art for the list (art/<game code>.jpg), built in.
file(GLOB GBAW_ART CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/art/*.jpg")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gen/art.h"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/gen"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_art.py" "${CMAKE_CURRENT_SOURCE_DIR}/art" "${CMAKE_CURRENT_BINARY_DIR}/gen/art.h"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_art.py" ${GBAW_ART}
    VERBATIM)
set(GBAW_SOURCES frontend/main.cpp frontend/third_party/xbrz/xbrz.cpp "${CMAKE_CURRENT_BINARY_DIR}/gen/patches.h" "${CMAKE_CURRENT_BINARY_DIR}/gen/art.h")
get_target_property(GBAW_MGBA_DEFS mgba COMPILE_DEFINITIONS)
if(GBAW_EMBEDDED)
    # The library for the launcher: the player without its own list (embed.h), on the
    # launcher's SDL2.
    add_library(gbawide STATIC ${GBAW_SOURCES})
    target_compile_definitions(gbawide PUBLIC GBAW_EMBEDDED)
    target_compile_definitions(gbawide PRIVATE ${GBAW_MGBA_DEFS} crc32=gbaw_crc32)
    target_include_directories(gbawide PUBLIC frontend)
    target_include_directories(gbawide PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gen")
    target_link_libraries(gbawide PRIVATE mgba)
    if(TARGET SDL2::SDL2)
        target_link_libraries(gbawide PRIVATE SDL2::SDL2)
    elseif(SDL2_STATIC_INCLUDE_DIRS)
        target_include_directories(gbawide PRIVATE ${SDL2_STATIC_INCLUDE_DIRS})
        target_compile_options(gbawide PRIVATE ${SDL2_STATIC_CFLAGS_OTHER})
        # (Static SDL2, the Switch's: named after this library too, so that what only it uses,
        # SDL_SaveBMP_RW say, is taken from libSDL2.a.)
        target_link_libraries(gbawide PRIVATE ${SDL2_STATIC_LIBRARIES})
    else()
        target_include_directories(gbawide PRIVATE ${SDL2_INCLUDE_DIRS})
    endif()
    if(ANDROID)
        target_link_libraries(gbawide PRIVATE android log)
    endif()
    return()
endif()
if(WIN32)
    # The program's icon, drawn by tools/pc/make_icon.py.
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gen/icon.ico" "${CMAKE_CURRENT_BINARY_DIR}/gen/icon.rc"
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/pc/make_icon.py" "${CMAKE_CURRENT_BINARY_DIR}/gen/icon.ico" "${CMAKE_CURRENT_BINARY_DIR}/gen/icon.rc"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/pc/make_icon.py"
        VERBATIM)
    list(APPEND GBAW_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/gen/icon.rc")
endif()
if(ANDROID)
    # On Android the program is the library libmain.so that SDL's Java activity loads
    # (tools/android/build.sh packs it into an APK).
    add_library(pokemon-gba-wide SHARED ${GBAW_SOURCES})
    set_target_properties(pokemon-gba-wide PROPERTIES OUTPUT_NAME main)
    target_link_libraries(pokemon-gba-wide PRIVATE android log)
else()
    add_executable(pokemon-gba-wide WIN32 ${GBAW_SOURCES})
endif()
target_include_directories(pokemon-gba-wide PRIVATE frontend "${CMAKE_CURRENT_BINARY_DIR}/gen")
target_link_libraries(pokemon-gba-wide PRIVATE mgba)
if(GBAW_SWITCH)
    target_include_directories(pokemon-gba-wide PRIVATE ${SDL2_STATIC_INCLUDE_DIRS})
    target_compile_options(pokemon-gba-wide PRIVATE ${SDL2_STATIC_CFLAGS_OTHER})
    target_link_directories(pokemon-gba-wide PRIVATE ${SDL2_STATIC_LIBRARY_DIRS})
    target_link_libraries(pokemon-gba-wide PRIVATE ${SDL2_STATIC_LIBRARIES} nx m)
else()
    if(TARGET SDL2::SDL2main)
        target_link_libraries(pokemon-gba-wide PRIVATE SDL2::SDL2main)
    endif()
    if(TARGET SDL2::SDL2)
        target_link_libraries(pokemon-gba-wide PRIVATE SDL2::SDL2)
    else()
        target_include_directories(pokemon-gba-wide PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(pokemon-gba-wide PRIVATE ${SDL2_LIBRARIES})
    endif()
endif()
# (mGBA's headers depend on the same feature defines it was built with: ENABLE_DIRECTORIES and
# others change what its structures hold.)
target_compile_definitions(pokemon-gba-wide PRIVATE ${GBAW_MGBA_DEFS})
if(UNIX AND NOT APPLE AND NOT GBAW_SWITCH AND NOT ANDROID)
    # (The Linux download has SDL2 beside the program.)
    set_target_properties(pokemon-gba-wide PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
endif()
if(MINGW)
    # One .exe with no MinGW runtime DLLs beside it (SDL2.dll is the only one).
    target_link_options(pokemon-gba-wide PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(GBAW_SWITCH)
    # The .nro for the Homebrew Menu, with libnx's default icon.
    find_program(GBAW_NACPTOOL nacptool HINTS "${DEVKITPRO}/tools/bin" NO_CMAKE_FIND_ROOT_PATH)
    find_program(GBAW_ELF2NRO elf2nro HINTS "${DEVKITPRO}/tools/bin" NO_CMAKE_FIND_ROOT_PATH)
    if(NOT GBAW_NACPTOOL OR NOT GBAW_ELF2NRO)
        message(FATAL_ERROR "nacptool and elf2nro were not found in ${DEVKITPRO}/tools/bin: install switch-dev with dkp-pacman")
    endif()
    add_custom_target(pokemon-gba-wide-nro ALL
        COMMAND "${GBAW_NACPTOOL}" --create "Pokemon GBA Widescreen" "mamesj" "1.0" "${CMAKE_CURRENT_BINARY_DIR}/pokemon-gba-wide.nacp"
        COMMAND "${GBAW_ELF2NRO}" "$<TARGET_FILE:pokemon-gba-wide>" "${CMAKE_CURRENT_BINARY_DIR}/pokemon-gba-wide.nro"
            "--nacp=${CMAKE_CURRENT_BINARY_DIR}/pokemon-gba-wide.nacp" "--icon=${DEVKITPRO}/libnx/default_icon.jpg"
        DEPENDS pokemon-gba-wide
        COMMENT "Making pokemon-gba-wide.nro"
        VERBATIM)
endif()
