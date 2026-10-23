# Phase 2 probe: compiles DAORecomp's generated game code (recomp/ppc, XenonRecomp output)
# for ARM64 Android as a static library, and a harness that runs its leaf functions and
# hashes the results, so an ARM64 run on the phone can be compared with an x86-64 run on
# the PC. It mirrors recomp/DAORecompLib/CMakeLists.txt with the clang-cl flags turned into
# GCC-style ones for the NDK's clang:
#   /fp:strict -> -ffp-model=strict      /clang:-ffp-contract=off -> -ffp-contract=off
#   /FI mmio_hook.h -> -include           -march=... -> none (arm64-v8a baseline)
#
# ARM64 (library + harness), see docs/PHASE2_runbook.md:
#   cmake -G Ninja -S android/ppc-probe -B build/ppc-probe \
#     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
# x86-64 Windows (harness only, against the lib DAORecomp's installer built):
#   cmake -G Ninja -S android/ppc-probe -B build/ppc-probe-x64 \
#     -DCMAKE_TOOLCHAIN_FILE=recomp/cmake/clang-cl-xwin.cmake \
#     -DDAO_LLVM_BIN=recomp/tools/llvm/bin -DDAO_WINSYSROOT=recomp/tools/winsysroot \
#     -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.22)
# The x64 library was built with the static CRT (DAORecomp's root CMakeLists.txt); match it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(DaoPpcProbe CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(DAO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(DAO_PPC_DIR ${DAO_ROOT}/recomp/ppc CACHE PATH "XenonRecomp output (generated on the PC)")
set(DAO_SIMDE_DIR ${DAO_ROOT}/recomp/tools/XenonRecomp/thirdparty/simde CACHE PATH "SIMDe (from XenonRecomp)")
set(DAO_MMIO_HOOK ${DAO_ROOT}/recomp/DAORecompLib/mmio_hook.h)
# The x86-64 library DAORecomp's installer built (phase 1), for the x64 harness.
set(DAO_X64_LIB ${DAO_ROOT}/recomp/build/installer/release/DAORecompLib/DAORecompLib.lib CACHE FILEPATH "DAORecompLib.lib from the PC build")
set(DAO_HARNESS_VMX 300 CACHE STRING "Leaf functions with VMX/FP work to include")
set(DAO_HARNESS_OTHER 100 CACHE STRING "Integer-only leaf functions to include")

if (NOT EXISTS ${DAO_PPC_DIR}/ppc_func_mapping.cpp)
    message(FATAL_ERROR "No generated code in ${DAO_PPC_DIR}: run DAORecomp's installer first (docs/PHASE1_runbook.md).")
endif()
if (NOT EXISTS ${DAO_SIMDE_DIR}/x86/sse.h)
    message(FATAL_ERROR "No SIMDe at ${DAO_SIMDE_DIR}: the installer's XenonRecomp checkout is missing.")
endif()

# ---------------------------------------------------------------- flags

if (MSVC)  # clang-cl (the xwin toolchain)
    set(DAO_PPC_FLAGS -fno-strict-aliasing /fp:strict /clang:-ffp-contract=off /clang:-march=x86-64-v3
        /FI${DAO_MMIO_HOOK} -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable)
else()
    set(DAO_PPC_FLAGS -fno-strict-aliasing -ffp-model=strict -ffp-contract=off
        -include ${DAO_MMIO_HOOK} -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable)
endif()

# ---------------------------------------------------------------- the generated code (ARM64)

if (ANDROID)
    file(GLOB DAO_PPC_SOURCES CONFIGURE_DEPENDS
        ${DAO_PPC_DIR}/ppc_recomp.*.cpp
        ${DAO_PPC_DIR}/ppc_func_mapping.cpp)
    list(LENGTH DAO_PPC_SOURCES _n)
    message(STATUS "Generated units: ${_n}")

    add_library(daoppc STATIC ${DAO_PPC_SOURCES})
    target_include_directories(daoppc PRIVATE ${DAO_PPC_DIR} ${DAO_SIMDE_DIR})
    target_compile_options(daoppc PRIVATE ${DAO_PPC_FLAGS})
    target_precompile_headers(daoppc PRIVATE ${DAO_PPC_DIR}/ppc_recomp_shared.h)
endif()

# ---------------------------------------------------------------- the harness (both hosts)

# The leaf-function table: scripts/find_leaf_funcs.py -> CSV -> scripts/gen_leaf_table.py -> .inc,
# generated at configure time into the build folder (it lists game function addresses).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(DAO_GEN ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${DAO_GEN})
if (NOT EXISTS ${DAO_GEN}/leaf_funcs.inc)
    execute_process(COMMAND ${Python3_EXECUTABLE} -I ${DAO_ROOT}/scripts/find_leaf_funcs.py --min-lines 6 --max 0
                            --csv ${DAO_GEN}/leaf_funcs.csv
                    RESULT_VARIABLE _r1 OUTPUT_VARIABLE _o1)
    execute_process(COMMAND ${Python3_EXECUTABLE} -I ${DAO_ROOT}/scripts/gen_leaf_table.py ${DAO_GEN}/leaf_funcs.csv
                            ${DAO_GEN}/leaf_funcs.inc --vmx ${DAO_HARNESS_VMX} --other ${DAO_HARNESS_OTHER}
                            --imports ${DAO_GEN}/import_stubs.inc
                    RESULT_VARIABLE _r2 OUTPUT_VARIABLE _o2)
    if (NOT _r1 EQUAL 0 OR NOT _r2 EQUAL 0)
        message(FATAL_ERROR "Leaf table generation failed:\n${_o1}\n${_o2}")
    endif()
    message(STATUS "${_o1}${_o2}")
endif()

add_executable(daoppc_harness harness.cpp)
target_include_directories(daoppc_harness PRIVATE ${DAO_PPC_DIR} ${DAO_SIMDE_DIR} ${DAO_GEN})
target_compile_options(daoppc_harness PRIVATE ${DAO_PPC_FLAGS})
if (ANDROID)
    # Only the leaf functions the harness names are kept (they call nothing).
    target_link_libraries(daoppc_harness PRIVATE daoppc -Wl,--gc-sections)
else()
    if (NOT EXISTS ${DAO_X64_LIB})
        message(FATAL_ERROR "No ${DAO_X64_LIB}: the PC build (phase 1) must exist for the x64 harness.")
    endif()
    target_link_libraries(daoppc_harness PRIVATE ${DAO_X64_LIB})
    target_link_options(daoppc_harness PRIVATE /OPT:REF)
endif()
