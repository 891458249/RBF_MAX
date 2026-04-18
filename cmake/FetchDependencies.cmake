# =============================================================================
# FetchDependencies.cmake — Pinned third-party dependencies for rbfmax.
# -----------------------------------------------------------------------------
# All dependencies are header-only or source-built from a pinned tag so that
# the project remains reproducible across the compiler matrix
# (MSVC 14.0 ↔ 17.3, GCC 4.8.2 ↔ 11.2.1).
#
# IMPORTANT — TBB is NOT fetched here. Per the project's technical ruling,
# Intel TBB MUST be linked from Maya's install directory at plug-in build
# time (Phase 2) to avoid ABI-level thread pool conflicts with the host.
# =============================================================================

include(FetchContent)

# Allow an offline/air-gapped mirror to override the canonical repos.
# Set RBFMAX_DEPS_MIRROR to e.g. "file:///mnt/mirror/" or "https://git.studio/"
# and the dependency URLs below will be rewritten accordingly.
set(RBFMAX_EIGEN_REPO      "https://gitlab.com/libeigen/eigen.git")
set(RBFMAX_GTEST_REPO      "https://github.com/google/googletest.git")
set(RBFMAX_JSON_REPO       "https://github.com/nlohmann/json.git")
set(RBFMAX_BENCHMARK_REPO  "https://github.com/google/benchmark.git")

if(DEFINED RBFMAX_DEPS_MIRROR)
    message(STATUS "rbfmax: using dependency mirror '${RBFMAX_DEPS_MIRROR}'")
    string(REGEX REPLACE "^https?://[^/]+/" "${RBFMAX_DEPS_MIRROR}"
           RBFMAX_EIGEN_REPO     "${RBFMAX_EIGEN_REPO}")
    string(REGEX REPLACE "^https?://[^/]+/" "${RBFMAX_DEPS_MIRROR}"
           RBFMAX_GTEST_REPO     "${RBFMAX_GTEST_REPO}")
    string(REGEX REPLACE "^https?://[^/]+/" "${RBFMAX_DEPS_MIRROR}"
           RBFMAX_JSON_REPO      "${RBFMAX_JSON_REPO}")
    string(REGEX REPLACE "^https?://[^/]+/" "${RBFMAX_DEPS_MIRROR}"
           RBFMAX_BENCHMARK_REPO "${RBFMAX_BENCHMARK_REPO}")
endif()

# ---------------------------------------------------------------------------
#  Eigen 3.3.9  (header-only; highest version still supporting GCC 4.8.2)
# ---------------------------------------------------------------------------
# We manually populate rather than FetchContent_MakeAvailable to avoid pulling
# in Eigen's helper targets (uninstall, docs, blas, tests) which pollute the
# top-level build graph and break 'ninja list' output.
FetchContent_Declare(Eigen3
    GIT_REPOSITORY "${RBFMAX_EIGEN_REPO}"
    GIT_TAG        3.3.9
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(Eigen3)
if(NOT eigen3_POPULATED)
    message(STATUS "rbfmax: fetching Eigen 3.3.9 ...")
    FetchContent_Populate(Eigen3)
endif()
if(NOT TARGET Eigen3::Eigen)
    add_library(rbfmax_eigen_iface INTERFACE)
    # SYSTEM keyword suppresses warnings originating from Eigen headers.
    target_include_directories(rbfmax_eigen_iface SYSTEM INTERFACE
        "${eigen3_SOURCE_DIR}"
    )
    # These definitions harden Eigen against surprises under our warning set
    # and the older GCC 4.8 alignment quirks.
    target_compile_definitions(rbfmax_eigen_iface INTERFACE
        EIGEN_MPL2_ONLY                # Forbid LGPL-licensed subfeatures.
        EIGEN_NO_DEBUG_ALIGNMENT_CHECK  # Avoid spurious asserts on GCC 4.8.
    )
    add_library(Eigen3::Eigen ALIAS rbfmax_eigen_iface)
endif()

# ---------------------------------------------------------------------------
#  GoogleTest 1.12.1  (last release supporting C++11)
# ---------------------------------------------------------------------------
if(RBF_BUILD_TESTS)
    set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)  # Match MSVC CRT.
    set(BUILD_GMOCK            ON  CACHE BOOL "" FORCE)
    set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        GIT_REPOSITORY "${RBFMAX_GTEST_REPO}"
        GIT_TAG        release-1.12.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)

    # GTest 1.12 emits a deprecation warning on newer GCC for its own tr1
    # shims. Silence this for test targets only, not for our code.
    if(TARGET gtest)
        set_target_properties(gtest      PROPERTIES FOLDER "external/gtest")
        set_target_properties(gtest_main PROPERTIES FOLDER "external/gtest")
    endif()
endif()

# ---------------------------------------------------------------------------
#  nlohmann/json 3.11.3  (header-only)  — deferred fetch: only if requested
# ---------------------------------------------------------------------------
function(rbfmax_fetch_nlohmann_json)
    if(TARGET nlohmann_json::nlohmann_json)
        return()
    endif()
    set(JSON_BuildTests          OFF CACHE INTERNAL "")
    set(JSON_Install             OFF CACHE INTERNAL "")
    set(JSON_SystemInclude       ON  CACHE INTERNAL "")
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY "${RBFMAX_JSON_REPO}"
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endfunction()

# ---------------------------------------------------------------------------
#  Google Benchmark 1.8.3  — deferred fetch: only if benchmarks enabled
# ---------------------------------------------------------------------------
function(rbfmax_fetch_google_benchmark)
    if(TARGET benchmark::benchmark)
        return()
    endif()
    set(BENCHMARK_ENABLE_TESTING      OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL      OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS  OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY "${RBFMAX_BENCHMARK_REPO}"
        GIT_TAG        v1.8.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(benchmark)
endfunction()
