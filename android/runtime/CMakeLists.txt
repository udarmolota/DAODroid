# DAORecomp's runtime for ARM64 Android: the kernel (threads, objects, memory, VFS, XAM),
# the XMA audio decoder, Xenia's Vulkan GPU (xenia-vulkan/), the null command processor,
# the game's hooks, menu patches, settings and the generated game code, as one static
# library (daodroid-runtime) with two front ends:
#   daodroid_headless  a command-line executable for adb: no window, no SDL (phases 3-4)
#   libdaodroid.so     the app's game library (android/app/src/main/cpp, phase 5)
# See docs/PHASE3_runbook.md, PHASE4_runbook.md and PHASE5_runbook.md.
#
#   cmake -G Ninja -S android/runtime -B build/runtime \
#     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.22)
project(DaoDroidRuntime C CXX ASM)

if (NOT ANDROID)
    message(FATAL_ERROR "This target is for the Android NDK toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(DAO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(RECOMP ${DAO_ROOT}/recomp)
set(DAO_SRC ${RECOMP}/DAORecomp)
set(XENON_ROOT ${RECOMP}/tools/XenonRecomp)
set(XENON_TP ${XENON_ROOT}/thirdparty)
set(XE_TP ${RECOMP}/tools/xenia-canary/third_party)

# ---------------------------------------------------------------- generated game code

add_subdirectory(${DAO_ROOT}/android/ppc-probe ${CMAKE_BINARY_DIR}/ppc)

# ---------------------------------------------------------------- Xenia (Vulkan GPU)

add_subdirectory(${DAO_ROOT}/xenia-vulkan ${CMAKE_BINARY_DIR}/xenia-vulkan)

# ---------------------------------------------------------------- XenonUtils (XEX loading)

add_library(XenonUtils STATIC
    ${XENON_ROOT}/XenonUtils/xex.cpp
    ${XENON_ROOT}/XenonUtils/image.cpp
    ${XENON_ROOT}/XenonUtils/xex_patcher.cpp
    ${XENON_ROOT}/XenonUtils/memory_mapped_file.cpp
    ${XENON_TP}/libmspack/libmspack/mspack/lzxd.c
    ${XENON_TP}/tiny-AES-c/aes.c)
target_include_directories(XenonUtils
    PUBLIC ${XENON_ROOT}/XenonUtils ${XENON_TP}/simde
    PRIVATE ${XENON_TP}/libmspack/libmspack/mspack ${XENON_TP}/tiny-AES-c ${XENON_TP}/TinySHA1)
target_compile_options(XenonUtils PRIVATE -w)

# ---------------------------------------------------------------- FFmpeg subset (XMA)

set(XE_TARGET_X86_64 FALSE)
set(XE_TARGET_AARCH64 TRUE)
# The NEON files (ffmpeg-xenia/config.h declares them external for aarch64), assembled by
# clang; xenia-canary's third_party CMake defines this helper, ffmpeg.cmake only calls it.
set(XE_HAS_GAS_ASM TRUE)
function(xe_add_gas_sources target)
    set_source_files_properties(${ARGN} PROPERTIES LANGUAGE ASM)
    target_sources(${target} PRIVATE ${ARGN})
endfunction()
include(${RECOMP}/xenia/ffmpeg.cmake)

# ---------------------------------------------------------------- the runtime

# The build descriptor the runtime reads the image layout and function table from
# (DAORecompLib/build_info.cpp, compiled per generated library as DAORecomp does).
add_library(daobuildinfo STATIC ${CMAKE_CURRENT_SOURCE_DIR}/build_info_android.cpp)
target_include_directories(daobuildinfo PRIVATE ${RECOMP}/ppc ${RECOMP}/DAORecompLib ${XENON_TP}/simde)
target_compile_definitions(daobuildinfo PRIVATE DAO_BUILD_INFO=DaoBuildDisc DAO_BUILD_MAPPINGS=PPCFuncMappings)

# Everything both front ends share. Not here: input/input.cpp and apu/audio_output.cpp
# (SDL3: real in the app, a shim in the headless build), the UI overlays (ImGui over the
# game: keyboard, notices, photo mode, cheats; not ported yet, stubbed by each front end).
add_library(daodroid-runtime STATIC
    ${DAO_SRC}/kernel/game_build.cpp
    ${DAO_SRC}/kernel/import_stubs.cpp
    ${DAO_SRC}/kernel/memory.cpp
    ${DAO_SRC}/kernel/module.cpp
    ${DAO_SRC}/kernel/objects.cpp
    ${DAO_SRC}/kernel/thread.cpp
    ${DAO_SRC}/kernel/title_update.cpp
    ${DAO_SRC}/kernel/title_update_version.cpp
    ${DAO_SRC}/kernel/vfs.cpp
    ${DAO_SRC}/kernel/xam.cpp
    ${DAO_SRC}/kernel/xboxkrnl_io.cpp
    ${DAO_SRC}/kernel/xboxkrnl_memory.cpp
    ${DAO_SRC}/kernel/xboxkrnl_rtl.cpp
    ${DAO_SRC}/kernel/xboxkrnl_threading.cpp
    ${DAO_SRC}/kernel/xnet.cpp
    ${DAO_SRC}/apu/audio.cpp
    ${DAO_SRC}/apu/xma_decoder.cpp
    ${DAO_SRC}/apu/xma_trace.cpp
    ${DAO_SRC}/gpu/command_processor.cpp
    ${DAO_SRC}/gpu/video.cpp
    ${DAO_SRC}/gpu/camera_fov.cpp
    ${DAO_SRC}/gpu/d3d_queries.cpp
    ${DAO_SRC}/gpu/effect_params.cpp
    ${DAO_SRC}/gpu/held_shot.cpp
    ${DAO_SRC}/gpu/xenia_memory.cpp
    ${DAO_SRC}/content/installed.cpp
    ${DAO_SRC}/content/save_backups.cpp
    ${DAO_SRC}/patches/patches.cpp
    ${DAO_SRC}/ui/erf.cpp
    ${DAO_SRC}/ui/menu_movies.cpp
    ${DAO_SRC}/ui/graphics_menu.cpp
    ${DAO_SRC}/settings.cpp
    ${DAO_SRC}/files.cpp
    ${DAO_SRC}/log.cpp
    ${DAO_SRC}/hash.cpp
    ${DAO_SRC}/program.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/profiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/xenia_host_vulkan.cpp)
target_include_directories(daodroid-runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DAO_SRC}
    ${RECOMP}/ppc
    ${RECOMP}/DAORecompLib
    ${RECOMP}/xenia/src
    ${XE_TP}/fmt/include
    ${XE_TP}/tomlplusplus/include
    ${XE_TP}/ffmpeg-xenia
    ${XE_TP}/FFmpeg
    ${XENON_TP}/TinySHA1)
target_compile_definitions(daodroid-runtime PUBLIC FMT_HEADER_ONLY=1)
# The runtime's own sources (DAORecomp/) are built with these; the generated code and
# Xenia have their own.
set(DAO_RUNTIME_OPTIONS
    -fexceptions -frtti
    -fno-strict-aliasing
    -ffp-contract=off
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function)
target_compile_options(daodroid-runtime PRIVATE ${DAO_RUNTIME_OPTIONS})
target_link_libraries(daodroid-runtime PUBLIC daobuildinfo daoppc XenonUtils dao-xenia-vulkan libavcodec libavutil log)

# ---------------------------------------------------------------- headless front end

add_executable(daodroid_headless
    ${DAO_SRC}/input/input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main_headless.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs.cpp)
# shim/SDL3/SDL.h: no SDL in the headless build (input.cpp keeps DAO_INPUT_SCRIPT).
target_include_directories(daodroid_headless PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim)
target_compile_definitions(daodroid_headless PRIVATE DAO_HEADLESS=1)
target_compile_options(daodroid_headless PRIVATE ${DAO_RUNTIME_OPTIONS})
target_link_libraries(daodroid_headless PRIVATE daodroid-runtime)
# The recompiled main thread runs on this thread's stack: as deep as the guest's.
target_link_options(daodroid_headless PRIVATE -Wl,-z,stack-size=16777216)
