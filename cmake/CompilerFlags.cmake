# =============================================================================
# CompilerFlags.cmake — Warning & optimisation flags for the rbfmax matrix.
# -----------------------------------------------------------------------------
# Usage:
#   rbfmax_apply_warnings(<target>)       # Strict warnings, no Eigen pollution.
#   rbfmax_apply_release_tuning(<target>) # SIMD & LTO where safe.
#
# Notes:
#   - We intentionally keep -Wconversion / -Wsign-conversion OFF by default
#     because Eigen 3.3.x is not clean under them and would force PRIVATE
#     suppressions across every translation unit.
#   - Fast-math is gated behind RBF_ENABLE_FAST_MATH because it violates the
#     IEEE-754 NaN/Inf propagation guarantees that the RBF solver relies on
#     (e.g. catastrophic cancellation during acos-based quaternion distance).
# =============================================================================

function(rbfmax_apply_warnings tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE
            /W4
            /permissive-
            /bigobj                          # Eigen template instantiations.
            /wd4127                          # "conditional expression is constant" (Eigen).
            /wd4714                          # __forceinline not inlined (Eigen).
            /EHsc                            # Standard C++ exception model.
            /Zc:__cplusplus                  # Report correct __cplusplus value.
        )
        if(RBF_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE /WX)
        endif()
    else()
        # GCC 4.8.2+ / Clang — flags cross-checked against the lowest compiler.
        target_compile_options(${tgt} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wdouble-promotion
            -Wformat=2
            -Wmissing-declarations
        )
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
           AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 6.0)
            target_compile_options(${tgt} PRIVATE
                -Wnull-dereference
                -Wduplicated-cond
                -Wlogical-op
            )
        endif()
        if(RBF_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(rbfmax_apply_release_tuning tgt)
    # Release-only tuning. Keep Debug untouched for deterministic stepping.
    if(MSVC)
        target_compile_options(${tgt} PRIVATE
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Release>:/Oi>         # Intrinsic functions.
            $<$<CONFIG:Release>:/GL>         # Whole-program optimization (link-time).
        )
        target_link_options(${tgt} PRIVATE
            $<$<CONFIG:Release>:/LTCG>
        )
        if(RBF_ENABLE_FAST_MATH)
            target_compile_options(${tgt} PRIVATE
                $<$<CONFIG:Release>:/fp:fast>
            )
        endif()
    else()
        target_compile_options(${tgt} PRIVATE
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Release>:-fno-math-errno>
        )
        if(RBF_ENABLE_FAST_MATH)
            # We allow reciprocal math but still guard against NaN generation
            # by not enabling -ffinite-math-only.
            target_compile_options(${tgt} PRIVATE
                $<$<CONFIG:Release>:-funsafe-math-optimizations>
            )
        endif()
    endif()
endfunction()
