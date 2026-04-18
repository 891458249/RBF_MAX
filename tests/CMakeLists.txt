# =============================================================================
# tests/CMakeLists.txt — rbfmax GoogleTest suite
# =============================================================================
# Each test binary corresponds to one kernel sub-module so that CI can shard
# them across build nodes.  All tests link the header-only kernel library
# and use `gtest_main` for the boilerplate entry point.
# =============================================================================

include(GoogleTest)

# ---------------------------------------------------------------------------
#  Helper — register a single test binary with the right flags & discovery.
# ---------------------------------------------------------------------------
function(rbfmax_add_test name)
    set(src "${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp")
    if(NOT EXISTS "${src}")
        message(FATAL_ERROR "rbfmax_add_test: missing source '${src}'")
    endif()

    add_executable(${name} "${src}")
    target_link_libraries(${name}
        PRIVATE
            rbfmax::kernel
            GTest::gtest
            GTest::gtest_main
    )
    # Tests get our strict warning set PLUS /bigobj etc. for gtest's template
    # instantiations.
    rbfmax_apply_warnings(${name})
    set_target_properties(${name} PROPERTIES
        FOLDER "tests"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests"
    )

    # gtest_discover_tests runs the binary at build time to enumerate each
    # TEST()/TEST_F(), giving CTest per-case reporting.  DISCOVERY_TIMEOUT
    # is generous to survive cold-start disk I/O on network shares.
    gtest_discover_tests(${name}
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        PROPERTIES
            LABELS "kernel"
            TIMEOUT 60
        DISCOVERY_TIMEOUT 30
    )
endfunction()

# ---------------------------------------------------------------------------
#  Test binaries (one per kernel sub-module — extend as modules ship).
# ---------------------------------------------------------------------------
rbfmax_add_test(test_kernel_functions)
