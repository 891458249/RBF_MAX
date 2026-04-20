# =============================================================================
# benchmarks/CMakeLists.txt — Slice 09 Google Benchmark suite
# -----------------------------------------------------------------------------
# Only engaged when -DRBF_BUILD_BENCHMARKS=ON.  Fetches Google Benchmark on
# demand (function defined in cmake/FetchDependencies.cmake), auto-discovers
# bench_*.cpp, and links each against rbfmax::kernel + rbfmax_solver.
#
# Each source file already calls BENCHMARK_MAIN() so we link only the main
# benchmark library (not benchmark_main).
# =============================================================================

if(NOT RBF_BUILD_BENCHMARKS)
    return()
endif()

include(FetchContent)
rbfmax_fetch_google_benchmark()

# Shared include path so bench_*.cpp can pick up common.hpp.
set(RBFMAX_BENCH_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}")

# Auto-discover bench_*.cpp files.  Slice 09 ships bench_kernel/solver/predict.
file(GLOB bench_sources "${CMAKE_CURRENT_SOURCE_DIR}/bench_*.cpp")
if(bench_sources)
    foreach(src ${bench_sources})
        get_filename_component(name "${src}" NAME_WE)
        add_executable(${name} "${src}")
        target_include_directories(${name} PRIVATE "${RBFMAX_BENCH_INCLUDE}")
        target_link_libraries(${name} PRIVATE
            rbfmax::kernel
            rbfmax_solver
            benchmark::benchmark
        )
        rbfmax_apply_warnings(${name})
        # Benchmarks must use the same release tuning as production so the
        # numbers reflect real deployment performance (LTO, /fp:precise, ...).
        rbfmax_apply_release_tuning(${name})
        set_target_properties(${name} PROPERTIES
            FOLDER "benchmarks"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/benchmarks"
        )
    endforeach()
endif()
