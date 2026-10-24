# The app's native side (phase 5): libdaodroid.so, the game as an SDL3 application
# (SDL owns the activity, the window, the gamepads and the audio device), on the runtime
# library from android/runtime and Xenia's Vulkan GPU painting into the window; and
# libvkdriverhook.so, part of the private Vulkan loader that runs the game on a Turnip
# build instead of the phone's driver (vulkan_private_loader.c). Built by Gradle
# (android/app/build.gradle), or directly:
#   cmake -G Ninja -S android/app/src/main/cpp -B build/app \
#     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.22)
project(DaoDroidApp C CXX ASM)

if (NOT ANDROID)
    message(FATAL_ERROR "This target is for the Android NDK toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DAO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
get_filename_component(DAO_ROOT ${DAO_ROOT} ABSOLUTE)
set(RECOMP ${DAO_ROOT}/recomp)
set(DAO_SRC ${RECOMP}/DAORecomp)

# ---------------------------------------------------------------- SDL3 (shared)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_ANDROID_JAR OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
add_subdirectory(${RECOMP}/thirdparty/SDL ${CMAKE_BINARY_DIR}/sdl)

# ---------------------------------------------------------------- the runtime

add_subdirectory(${DAO_ROOT}/android/runtime ${CMAKE_BINARY_DIR}/runtime)

# ---------------------------------------------------------------- private Vulkan loader

# liblinkernsbypass (BSD-2-Clause, Billy Laws): linker namespaces for the private loader.
add_library(linkernsbypass STATIC
    linkernsbypass/android_linker_ns.cpp
    linkernsbypass/elf_soname_patcher.cpp)
target_include_directories(linkernsbypass PUBLIC linkernsbypass)
target_compile_options(linkernsbypass PRIVATE -w)
target_link_libraries(linkernsbypass android dl)

# Loaded into the private loader's namespace ahead of the system libvulkan.so copy;
# hands that copy the chosen driver when it looks for the phone's (-z global: it comes
# before libdl for every library in that namespace).
add_library(vkdriverhook SHARED vkdriverhook.c)
target_compile_options(vkdriverhook PRIVATE -fvisibility=hidden -Wall -Wextra)
target_link_options(vkdriverhook PRIVATE -Wl,-z,global -Wl,-z,max-page-size=16384)

# ---------------------------------------------------------------- the game library

add_library(daodroid SHARED
    ${DAO_SRC}/input/input.cpp
    ${DAO_SRC}/apu/audio_output.cpp
    app_main.cpp
    app_stubs.cpp
    sdl_window.cpp
    virtual_pad_jni.cpp
    keyboard_android.cpp
    vulkan_private_loader.c)
target_compile_options(daodroid PRIVATE ${DAO_RUNTIME_OPTIONS})
# The runtime's kernel imports and hooks register through static initializers: the whole
# archive goes in, not only what is referenced.
target_link_libraries(daodroid PRIVATE -Wl,--whole-archive daodroid-runtime -Wl,--no-whole-archive SDL3::SDL3 linkernsbypass android log)
# SDL_main must be visible to SDL's Java side (dlsym); the rest stays hidden.
# -Bsymbolic: FFmpeg's NEON assembly addresses its tables directly (no GOT).
target_link_options(daodroid PRIVATE -Wl,--gc-sections -Wl,-Bsymbolic -Wl,-z,max-page-size=16384)
