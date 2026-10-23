# Xenia's Vulkan GPU backend for the DAO Android runtime: the shared GPU, UI/presenter
# and base libraries from DAORecomp's vendored, patched Xenia (recomp/xenia, Direct3D 12
# only there), plus the Vulkan backend and SPIR-V shader translator imported here from
# the same xenia-canary commit (src/xenia/{gpu/vulkan,gpu/spirv_*,ui/vulkan}; see
# PATCHES.md). Built for Android only; included by android/runtime/CMakeLists.txt.
#
# Third-party code comes from the installer's xenia-canary checkout
# (recomp/tools/xenia-canary/third_party): Vulkan-Headers, VulkanMemoryAllocator,
# glslang (its SPIRV builder), FidelityFX-CAS and -FSR (shader includes) must be
# initialized there (docs/PHASE4_runbook.md). Shaders are compiled to SPIR-V at build
# time with the NDK's shader tools (scripts/build_xenia_shaders_spirv.py).

if(NOT ANDROID)
    message(FATAL_ERROR "xenia-vulkan is built for the Android NDK toolchain only.")
endif()

set(XV_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(XV_SRC ${XV_ROOT}/src/xenia)
get_filename_component(DAO_ROOT ${XV_ROOT} DIRECTORY)
set(XE_DAO ${DAO_ROOT}/recomp/xenia)
set(XE_SRC ${XE_DAO}/src/xenia)
set(XE_CANARY ${DAO_ROOT}/recomp/tools/xenia-canary)
set(XE_TP ${XE_CANARY}/third_party)
# Xenia includes them as "third_party/<library>/...".
get_filename_component(XE_TP_PARENT ${XE_TP} DIRECTORY)
set(XV_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)

foreach(dep Vulkan-Headers/include/vulkan/vulkan.h VulkanMemoryAllocator/include/vk_mem_alloc.h
            glslang/SPIRV/SpvBuilder.h FidelityFX-CAS/ffx-cas/ffx_cas.h FidelityFX-FSR/ffx-fsr/ffx_fsr1.h)
    if(NOT EXISTS ${XE_TP}/${dep})
        message(FATAL_ERROR "Missing ${XE_TP}/${dep}: initialize the xenia-canary submodules "
            "(docs/PHASE4_runbook.md).")
    endif()
endforeach()

# Sources of one Xenia directory, filtered like Xenia's xe_platform_sources for Android:
# no tests/mains/demos; the POSIX, Android and ARM64 variants of platform files only.
function(xv_sources target dir)
    file(GLOB _all CONFIGURE_DEPENDS ${dir}/*.cc ${dir}/*.cpp ${dir}/*.c)
    set(_sources)
    foreach(src ${_all})
        get_filename_component(_name ${src} NAME_WE)
        if(_name MATCHES "_(main|test|demo)$")
            continue()
        endif()
        if(_name MATCHES "_(win|linux|gnulinux|x11|gtk|mac|amd64|x64)$")
            continue()
        endif()
        list(APPEND _sources ${src})
    endforeach()
    target_sources(${target} PRIVATE ${_sources})
endfunction()

function(xv_exclude target regex)
    get_target_property(_srcs ${target} SOURCES)
    list(FILTER _srcs EXCLUDE REGEX "${regex}")
    set_target_properties(${target} PROPERTIES SOURCES "${_srcs}")
endfunction()

# Defines that headers depend on (seen by users of the libraries too).
set(XV_PUBLIC_DEFINES
    XE_OPTION_PROFILING=0
    XE_OPTION_PROFILING_UI=0
    IMGUI_DISABLE_DEFAULT_FONT
    IMGUI_USE_STB_SPRINTF
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

function(xv_defaults target)
    # The imported files first (xenia/gpu/vulkan, xenia/ui/vulkan, xenia/gpu/spirv_*),
    # then DAORecomp's tree (the shared GPU, UI and base code, memory.h, dao_host.h).
    target_include_directories(${target} PUBLIC ${XV_ROOT}/src ${XE_DAO} ${XE_DAO}/src)
    target_include_directories(${target} SYSTEM PUBLIC ${XE_TP} ${XE_TP_PARENT}
        ${XE_TP}/Vulkan-Headers/include ${XE_TP}/glslang)
    # vulkan_api.h includes the C++ bindings' to_string only; without this the
    # bindings' exception classes want it declared first (clang, libc++).
    target_compile_definitions(${target} PUBLIC ${XV_PUBLIC_DEFINES} VULKAN_HPP_NO_EXCEPTIONS)
    target_compile_options(${target} PRIVATE -w -fexceptions -frtti)
endfunction()

# ---------------------------------------------------------------- third party

add_library(fmt STATIC ${XE_TP}/fmt/src/format.cc ${XE_TP}/fmt/src/os.cc)
target_include_directories(fmt PUBLIC ${XE_TP}/fmt/include)
target_compile_options(fmt PRIVATE -w)

add_library(xxhash STATIC ${XE_TP}/xxhash/xxhash.c)
target_include_directories(xxhash PUBLIC ${XE_TP}/xxhash)
target_compile_options(xxhash PRIVATE -w)

add_library(dxbc STATIC ${XE_TP}/dxbc/DXBCChecksum.cpp)
target_include_directories(dxbc PUBLIC ${XE_TP}/dxbc)
target_compile_options(dxbc PRIVATE -w)

set(HAVE_SYS_UIO_H_01 1)
set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 2)
set(PROJECT_VERSION_PATCH 1)
configure_file(${XE_TP}/snappy/snappy-stubs-public.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/snappy_gen/snappy-stubs-public.h)
add_library(snappy STATIC
    ${XE_TP}/snappy/snappy-sinksource.cc
    ${XE_TP}/snappy/snappy-stubs-internal.cc
    ${XE_TP}/snappy/snappy.cc)
target_include_directories(snappy PUBLIC ${XE_TP}/snappy ${CMAKE_CURRENT_BINARY_DIR}/snappy_gen)
target_compile_options(snappy PRIVATE -w)

add_library(imgui STATIC
    ${XE_TP}/imgui/imgui.cpp
    ${XE_TP}/imgui/imgui_demo.cpp
    ${XE_TP}/imgui/imgui_draw.cpp
    ${XE_TP}/imgui/imgui_tables.cpp
    ${XE_TP}/imgui/imgui_widgets.cpp)
target_include_directories(imgui PUBLIC ${XE_TP}/imgui PRIVATE ${XE_TP}/stb)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEFAULT_FONT IMGUI_USE_STB_SPRINTF IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_options(imgui PRIVATE -w)

# glslang's SPIR-V builder (the shader translator emits SPIR-V through it); not the
# GLSL front end.
file(GLOB _glslang_spirv CONFIGURE_DEPENDS ${XE_TP}/glslang/SPIRV/*.cpp)
list(FILTER _glslang_spirv EXCLUDE REGEX "GlslangToSpv\\.cpp$")
add_library(glslang-spirv STATIC ${_glslang_spirv})
target_include_directories(glslang-spirv PUBLIC ${XE_TP}/glslang)
target_compile_options(glslang-spirv PRIVATE -w -fexceptions -frtti)

# ---------------------------------------------------------------- SPIR-V shaders

find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(NOT DEFINED DAO_SHADER_TOOLS)
    set(DAO_SHADER_TOOLS ${ANDROID_NDK}/shader-tools/${ANDROID_HOST_TAG} CACHE PATH
        "Directory holding glslc, spirv-opt and spirv-dis")
endif()
set(_shader_script ${DAO_ROOT}/scripts/build_xenia_shaders_spirv.py)
file(GLOB _shader_sources CONFIGURE_DEPENDS
    ${XE_SRC}/gpu/shaders/*.xesl ${XE_SRC}/gpu/shaders/*.xesli ${XE_SRC}/gpu/shaders/*.glsl
    ${XE_SRC}/ui/shaders/*.xesl ${XE_SRC}/ui/shaders/*.xesli)
set(_shader_stamp ${CMAKE_CURRENT_BINARY_DIR}/spirv_shaders.stamp)
add_custom_command(
    OUTPUT ${_shader_stamp}
    COMMAND ${Python3_EXECUTABLE} ${_shader_script} --tools ${DAO_SHADER_TOOLS}
        --include ${XE_CANARY}/src/xenia/ui/shaders
        ${XE_SRC}/gpu/shaders ${XV_GEN}/xenia/gpu/shaders/bytecode/vulkan_spirv
        ${XE_SRC}/ui/shaders ${XV_GEN}/xenia/ui/shaders/bytecode/vulkan_spirv
    COMMAND ${CMAKE_COMMAND} -E touch ${_shader_stamp}
    DEPENDS ${_shader_sources} ${_shader_script}
    COMMENT "Compiling Xenia's shaders to SPIR-V"
    VERBATIM)
add_custom_target(xenia-spirv-shaders DEPENDS ${_shader_stamp})

# ---------------------------------------------------------------- xenia

add_library(xenia-base STATIC)
xv_sources(xenia-base ${XE_SRC}/base)
# platform_arm64.cc needs xbyak_aarch64 (CPU feature flags for a JIT; nothing here
# calls it).
xv_exclude(xenia-base "(console_app_main_|main_init_|platform_arm64\\.cc)")
target_link_libraries(xenia-base PUBLIC fmt PRIVATE log dl)
xv_defaults(xenia-base)

add_library(xenia-ui STATIC)
xv_sources(xenia-ui ${XE_SRC}/ui)
# No Xenia window or app context on Android: the surface comes from the app (phase 5);
# no file picker, no microprofile.
xv_exclude(xenia-ui "(windowed_app_main_|windowed_app_android|windowed_app_context_android|window_android|file_picker_|microprofile_drawer)")
target_link_libraries(xenia-ui PUBLIC xenia-base imgui PRIVATE android)
xv_defaults(xenia-ui)

add_library(xenia-ui-vulkan STATIC)
xv_sources(xenia-ui-vulkan ${XV_SRC}/ui/vulkan)
target_include_directories(xenia-ui-vulkan BEFORE PUBLIC ${XV_GEN})
target_link_libraries(xenia-ui-vulkan PUBLIC xenia-base xenia-ui PRIVATE dl)
add_dependencies(xenia-ui-vulkan xenia-spirv-shaders)
xv_defaults(xenia-ui-vulkan)

add_library(xenia-gpu STATIC)
xv_sources(xenia-gpu ${XE_SRC}/gpu)
file(GLOB _spirv_translator CONFIGURE_DEPENDS ${XV_SRC}/gpu/spirv_*.cc)
target_sources(xenia-gpu PRIVATE ${_spirv_translator})
target_link_libraries(xenia-gpu PUBLIC dxbc fmt glslang-spirv snappy xxhash xenia-base xenia-ui)
xv_defaults(xenia-gpu)

add_library(xenia-gpu-vulkan STATIC)
xv_sources(xenia-gpu-vulkan ${XV_SRC}/gpu/vulkan)
target_include_directories(xenia-gpu-vulkan BEFORE PUBLIC ${XV_GEN})
target_link_libraries(xenia-gpu-vulkan PUBLIC fmt glslang-spirv xxhash xenia-base xenia-gpu xenia-ui xenia-ui-vulkan)
add_dependencies(xenia-gpu-vulkan xenia-spirv-shaders)
xv_defaults(xenia-gpu-vulkan)

# Everything the runtime links. Vulkan itself is loaded at run time (libvulkan.so).
add_library(dao-xenia-vulkan INTERFACE)
target_link_libraries(dao-xenia-vulkan INTERFACE xenia-gpu-vulkan xenia-gpu xenia-ui-vulkan xenia-ui xenia-base
    android log dl)
