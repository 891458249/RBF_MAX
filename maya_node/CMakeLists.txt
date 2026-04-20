# =============================================================================
# maya_node/CMakeLists.txt — Phase 2A Slice 10A (Maya 2022 anchor)
# -----------------------------------------------------------------------------
# Two independent opt-ins drive this file (top-level CMakeLists.txt):
#   * RBF_BUILD_MAYA_NODE          — the .mll/.so plugin; requires devkit
#   * RBF_BUILD_MAYA_ADAPTER_TESTS — pure-C++ adapter tests; no devkit
# =============================================================================

# ---------------------------------------------------------------------------
# Maya plugin (requires devkit)
# ---------------------------------------------------------------------------
if(RBF_BUILD_MAYA_NODE)
    include(${CMAKE_SOURCE_DIR}/cmake/MayaVersionMatrix.cmake)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
    find_package(Maya REQUIRED COMPONENTS OpenMaya OpenMayaAnim Foundation)

    # ------------------------------------------------------------------
    # Generate plugin_info.hpp from the .in template.  Values embedded
    # into the plugin at compile time for MFnPlugin registration and
    # future diagnostic use.
    # ------------------------------------------------------------------
    set(RBFMAX_MAYA_PLUGIN_VERSION "1.0.0-phase2a-slice10a")
    set(RBFMAX_MAYA_PLUGIN_VENDOR  "891458249 / RBF_MAX")
    # R-09 self-check: 0x00013A00 = 80384 (< 0x7FFFF development-range cap).
    set(RBFMAX_MAYA_NODE_TYPEID    "0x00013A00")
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/include/rbfmax/maya/plugin_info.hpp.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/rbfmax/maya/plugin_info.hpp
        @ONLY
    )

    add_library(rbfmax_maya_node MODULE
        src/mrbf_node.cpp
        src/plugin_main.cpp
    )

    target_include_directories(rbfmax_maya_node PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_link_libraries(rbfmax_maya_node PRIVATE
        rbfmax::kernel        # Phase 1 INTERFACE target (headers-only)
        Maya::Maya
    )

    target_compile_features(rbfmax_maya_node PRIVATE cxx_std_${MAYA_CXX_STD})

    # D12 — platform-specific Maya plugin suffix.
    if(WIN32)
        set_target_properties(rbfmax_maya_node PROPERTIES
            PREFIX ""
            SUFFIX ".mll"
            OUTPUT_NAME "rbfmax_maya"
        )
    elseif(APPLE)
        set_target_properties(rbfmax_maya_node PROPERTIES
            PREFIX ""
            SUFFIX ".bundle"
            OUTPUT_NAME "rbfmax_maya"
        )
    else()
        set_target_properties(rbfmax_maya_node PROPERTIES
            PREFIX ""
            OUTPUT_NAME "rbfmax_maya"
        )
    endif()

    # Maya headers demand NOMINMAX on Windows (R-19) — the macros collide
    # with std::min / std::max and Eigen's internal use of them.
    if(WIN32)
        target_compile_definitions(rbfmax_maya_node PRIVATE
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            REQUIRE_IOSTREAM
        )
        # /utf-8 — Phase 1 source files carry Chinese UTF-8 comments.
        # The project-wide rbfmax_apply_warnings() sets this, but we
        # deliberately skip that helper on the Maya target (Maya
        # headers break /WX); we still need /utf-8 to keep MSVC from
        # spewing C4819 on every Phase 1 header included transitively.
        target_compile_options(rbfmax_maya_node PRIVATE /utf-8)
    endif()

    # Release tuning only — deliberately NOT applying rbfmax_apply_warnings()
    # here.  Maya's public headers are SYSTEM-included via FindMaya's
    # INTERFACE_SYSTEM_INCLUDE_DIRECTORIES, but MSVC's /WX still bites on
    # some Maya macro expansions that the preprocessor emits from OUR
    # translation units.  Slice 10B+ will reintroduce strict warnings
    # behind /external:W0 (MSVC ≥ 16.10) or -isystem (Clang / GCC).
    rbfmax_apply_release_tuning(rbfmax_maya_node)

    # Place the plugin under build/bin for easy discovery by the mayapy
    # smoke script (symmetry with Phase 1's test binary layout).
    set_target_properties(rbfmax_maya_node PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# ---------------------------------------------------------------------------
# Pure-C++ adapter tests (no devkit required, runs on any CI node)
# ---------------------------------------------------------------------------
if(RBF_BUILD_MAYA_ADAPTER_TESTS)
    add_subdirectory(tests)
endif()
