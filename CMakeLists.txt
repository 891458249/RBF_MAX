# =============================================================================
# RBF_MAX — Industrial-grade Radial Basis Function plugin for Autodesk Maya
# -----------------------------------------------------------------------------
# Phase 1: Pure C++11 mathematical kernel (Maya-free, engine-agnostic).
# Target compilers:
#   - MSVC 14.0 (2015) through MSVC 17.x (2022)
#   - GCC 4.8.2 (RHEL 6 / CentOS 6, Maya 2018) through GCC 11.2.1 (Maya 2025)
# =============================================================================
cmake_minimum_required(VERSION 3.14)

project(rbfmax
    VERSION     1.0.0
    DESCRIPTION "Industrial-grade RBF interpolation kernel and Maya plug-in"
    LANGUAGES   CXX
)

# --- Policies ---------------------------------------------------------------
# Allow FetchContent_Populate (single-arg form) on CMake 3.28+.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# --- Global build configuration --------------------------------------------
set(CMAKE_CXX_STANDARD          11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Honour user-specified build type; default to Release for safety.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# Position-independent code is required on Linux for eventual Maya .so linking.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Quieter install output; keep binaries predictable.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# --- Options ----------------------------------------------------------------
option(RBF_BUILD_TESTS         "Build GoogleTest-based unit tests"  ON)
option(RBF_BUILD_BENCHMARKS    "Build Google Benchmark suites"      OFF)
option(RBF_WARNINGS_AS_ERRORS  "Treat warnings as errors (/WX -Werror)" ON)
option(RBF_ENABLE_FAST_MATH    "Enable fast-math (breaks NaN-safety!)"  OFF)

# --- Module path & helpers --------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerFlags)
include(FetchDependencies)

# --- Kernel library (header-only, Maya-free) --------------------------------
add_library(rbfmax_kernel INTERFACE)
add_library(rbfmax::kernel ALIAS rbfmax_kernel)

target_include_directories(rbfmax_kernel INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/kernel/include>"
    "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(rbfmax_kernel INTERFACE
    Eigen3::Eigen
)

target_compile_features(rbfmax_kernel INTERFACE cxx_std_11)

# Export the project version as a compile definition so the kernel can
# embed it into serialized assets.
target_compile_definitions(rbfmax_kernel INTERFACE
    RBFMAX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    RBFMAX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    RBFMAX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# --- nlohmann/json (Slice 08: first real consumer) --------------------------
# FetchDependencies.cmake exposes this as a deferred fetch function so
# downstream consumers that never serialize don't pay the clone cost.
# Slice 08 introduces the first real consumer (rbfmax::io_json) so we call
# it unconditionally here.
rbfmax_fetch_nlohmann_json()

# --- Solver library (non-header-only; first .cpp) ---------------------------
# Slice 05: introduces the first compiled translation unit. Built as STATIC
# so downstream Maya plug-in DLLs can link without exposing rbfmax internals
# in their export tables.
# Slice 06: predict ScratchPool (in solver.cpp).
# Slice 07: RBFInterpolator facade (interpolator.cpp).
# Slice 08: JSON I/O (io_json.cpp) — adds nlohmann_json as a transitive
#           PUBLIC dependency.
add_library(rbfmax_solver STATIC
    kernel/src/solver.cpp
    kernel/src/interpolator.cpp
    kernel/src/io_json.cpp
)
add_library(rbfmax::solver ALIAS rbfmax_solver)

target_include_directories(rbfmax_solver PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/kernel/include>"
    "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(rbfmax_solver PUBLIC
    rbfmax::kernel
    nlohmann_json::nlohmann_json
)

target_compile_features(rbfmax_solver PUBLIC cxx_std_11)

rbfmax_apply_warnings(rbfmax_solver)
rbfmax_apply_release_tuning(rbfmax_solver)

target_compile_definitions(rbfmax_solver PUBLIC
    RBFMAX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    RBFMAX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    RBFMAX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# --- Subdirectories ---------------------------------------------------------
if(RBF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RBF_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# --- Summary ----------------------------------------------------------------
message(STATUS "")
message(STATUS "========== rbfmax configuration ==========")
message(STATUS "  Version              : ${PROJECT_VERSION}")
message(STATUS "  Build type           : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard         : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler             : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build tests          : ${RBF_BUILD_TESTS}")
message(STATUS "  Build benchmarks     : ${RBF_BUILD_BENCHMARKS}")
message(STATUS "  Warnings-as-errors   : ${RBF_WARNINGS_AS_ERRORS}")
message(STATUS "  Fast math            : ${RBF_ENABLE_FAST_MATH}")
message(STATUS "==========================================")
message(STATUS "")
