# =============================================================================
# benchmarks/CMakeLists.txt — Slice 05 skeleton
# -----------------------------------------------------------------------------
# Real benchmark sources (bench_*.cpp) will land in Slice 09. This file exists
# now so that -DRBF_BUILD_BENCHMARKS=ON has a valid add_subdirectory target,
# clearing the dangling reference at the top-level CMakeLists.txt that has
# been outstanding since Slice 01 (R-06 in DEVLOG).
# =============================================================================

if(NOT RBF_BUILD_BENCHMARKS)
    return()
endif()

include(FetchContent)
rbfmax_fetch_google_benchmark()

# Auto-discover bench_*.cpp files; none exist yet (Slice 09 will add them).
file(GLOB bench_sources "${CMAKE_CURRENT_SOURCE_DIR}/bench_*.cpp")
if(bench_sources)
    foreach(src ${bench_sources})
        get_filename_component(name "${src}" NAME_WE)
        add_executable(${name} "${src}")
        target_link_libraries(${name} PRIVATE
            rbfmax::kernel
            rbfmax::solver
            benchmark::benchmark
            benchmark::benchmark_main
        )
        rbfmax_apply_warnings(${name})
        rbfmax_apply_release_tuning(${name})
        set_target_properties(${name} PROPERTIES
            FOLDER "benchmarks"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/benchmarks"
        )
    endforeach()
endif()
