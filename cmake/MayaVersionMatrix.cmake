# =============================================================================
# cmake/MayaVersionMatrix.cmake — Phase 2A Slice 10A
# -----------------------------------------------------------------------------
# Maps MAYA_VERSION to C++ standard requirement and devkit probe paths.
# MUST be included BEFORE FindMaya.cmake (it defines
# ``MAYA_DEVKIT_PROBE_PATHS`` which FindMaya.cmake falls back to).
#
# Slice 10A anchors Maya 2022 (tightest C++ standard, shift-left).
# 10B / 10C / 10D will extend to 2024 / 2025 / 2026 respectively; this
# file already accepts those versions so the validation slices should
# only need FindMaya.cmake tweaks (if any).
# =============================================================================

if(NOT DEFINED MAYA_VERSION)
    set(MAYA_VERSION "2022" CACHE STRING
        "Autodesk Maya version to build plugin for (2022|2024|2025|2026)")
endif()

set(_rbf_valid_maya_versions "2022;2024;2025;2026")
if(NOT MAYA_VERSION IN_LIST _rbf_valid_maya_versions)
    message(FATAL_ERROR
        "Unsupported MAYA_VERSION: ${MAYA_VERSION}. "
        "Must be one of: ${_rbf_valid_maya_versions}")
endif()

# ---------------------------------------------------------------------------
# C++ standard matrix (per Phase 2A D4' decision after Slice 10A re-anchor).
# Maya 2022 ABI ships against VS2019 + C++14; 2024/2025/2026 move to C++17.
# Slice 10A targets the tightest (2022 ⇒ C++14) so later versions are a
# no-op at the standard level.
# ---------------------------------------------------------------------------
if(MAYA_VERSION STREQUAL "2022")
    set(MAYA_CXX_STD 14)
else()
    set(MAYA_CXX_STD 17)
endif()

# ---------------------------------------------------------------------------
# Probe paths used when MAYA_DEVKIT_ROOT is not supplied explicitly.
#
# Note on Maya 2022: Autodesk moved the devkit out of the main install in
# 2022+ (``<install>/devkit/`` now contains only a README pointing to an
# external download).  However the main Maya install still ships the
# most-used headers under ``<install>/include/maya/`` and import libs under
# ``<install>/lib/``.  FindMaya.cmake therefore accepts either layout:
#
#   * "installed devkit"    — ``<ROOT>/include/maya/MFn.h``
#   * "separate devkit pkg" — ``<ROOT>/devkit/include/maya/MFn.h``
# ---------------------------------------------------------------------------
if(WIN32)
    set(MAYA_DEVKIT_PROBE_PATHS
        "C:/Program Files/Autodesk/Maya${MAYA_VERSION}/devkit"
        "C:/Program Files/Autodesk/Maya${MAYA_VERSION}"
        "C:/Autodesk/Maya${MAYA_VERSION}"
        "C:/Autodesk/Maya${MAYA_VERSION}/devkit"
    )
elseif(UNIX AND APPLE)
    set(MAYA_DEVKIT_PROBE_PATHS
        "/Applications/Autodesk/maya${MAYA_VERSION}/Maya.app/Contents"
        "/Applications/Autodesk/maya${MAYA_VERSION}/devkit"
    )
elseif(UNIX)
    set(MAYA_DEVKIT_PROBE_PATHS
        "/usr/autodesk/maya${MAYA_VERSION}/devkit"
        "/usr/autodesk/maya${MAYA_VERSION}-x64/devkit"
        "/usr/autodesk/maya${MAYA_VERSION}"
        "/usr/autodesk/maya${MAYA_VERSION}-x64"
        "/opt/autodesk/maya${MAYA_VERSION}"
    )
endif()

message(STATUS "Maya target version: ${MAYA_VERSION} (C++${MAYA_CXX_STD})")
