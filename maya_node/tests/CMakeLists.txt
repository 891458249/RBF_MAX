# =============================================================================
# maya_node/tests/CMakeLists.txt — Phase 2A Slice 10A
# -----------------------------------------------------------------------------
# Pure-C++ adapter tests.  No Maya devkit required — these run on every
# CI node and exercise the kernel-linking layer only.
# =============================================================================

# GoogleTest is already fetched by the top-level tests/ subdirectory when
# RBF_BUILD_TESTS is ON.  Slice 10A documents a hard requirement on that
# flag so the adapter tests can re-use the GTest targets.
if(NOT TARGET GTest::gtest_main)
    message(FATAL_ERROR
        "maya_node/tests requires RBF_BUILD_TESTS=ON so that GoogleTest "
        "has been fetched and the GTest::gtest_main target is available.")
endif()

include(GoogleTest)

add_executable(test_adapter_core
    test_adapter_core.cpp
    test_draw_sink.cpp                                          # Slice 13
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/adapter_core_csv.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/draw_sink_core.cpp)      # Slice 13

target_link_libraries(test_adapter_core PRIVATE
    rbfmax::kernel
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(test_adapter_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Adapter tests build at C++17 — they are CI-side and do not need to
# honour the Maya 2022 C++14 ABI ceiling.  adapter_core.hpp is written
# against the C++14 subset so it compiles cleanly at either level.
target_compile_features(test_adapter_core PRIVATE cxx_std_17)

rbfmax_apply_warnings(test_adapter_core)
rbfmax_apply_release_tuning(test_adapter_core)

set_target_properties(test_adapter_core PROPERTIES
    FOLDER "tests"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests"
)

gtest_discover_tests(test_adapter_core
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    PROPERTIES
        LABELS "maya_adapter"
        TIMEOUT 30
    DISCOVERY_TIMEOUT 30
)
