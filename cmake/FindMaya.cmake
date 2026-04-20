# =============================================================================
# cmake/FindMaya.cmake — Phase 2A Slice 10A
# -----------------------------------------------------------------------------
# In-house FindMaya module.  Handwritten rather than taken from a third
# party so we own the probe/resolution rules for every supported version.
#
# Resolution priority for devkit root (highest wins):
#   1. -DMAYA_DEVKIT_ROOT=<path>       (explicit cache / command-line)
#   2. $ENV{MAYA_DEVKIT_ROOT}          (exported env var)
#   3. $ENV{MAYA_LOCATION}             (Maya's native env; usually the
#                                       main install, not a separate devkit)
#   4. MAYA_DEVKIT_PROBE_PATHS         (from MayaVersionMatrix.cmake)
#
# Layout tolerance — both of the following work as a "root":
#   <ROOT>/include/maya/MFn.h       (Maya's bundled headers)
#   <ROOT>/devkit/include/maya/MFn.h (separate Devkit archive)
#
# Exports:
#   Maya::OpenMaya        IMPORTED UNKNOWN, single library
#   Maya::OpenMayaAnim    IMPORTED UNKNOWN, single library (optional)
#   Maya::Foundation      IMPORTED UNKNOWN, single library
#   Maya::Maya            INTERFACE aggregate of the requested components
#
# All IMPORTED targets receive SYSTEM-marked include directories, so
# Maya headers do not pollute the project's warning-as-error surface
# (D13 mitigation).
# =============================================================================

include(FindPackageHandleStandardArgs)

# ---------------------------------------------------------------------------
# Assemble the search hints list from the priority chain above.
# ---------------------------------------------------------------------------
set(_maya_hints "")
if(DEFINED MAYA_DEVKIT_ROOT AND MAYA_DEVKIT_ROOT)
    list(APPEND _maya_hints "${MAYA_DEVKIT_ROOT}")
endif()
if(DEFINED ENV{MAYA_DEVKIT_ROOT} AND NOT "$ENV{MAYA_DEVKIT_ROOT}" STREQUAL "")
    list(APPEND _maya_hints "$ENV{MAYA_DEVKIT_ROOT}")
endif()
if(DEFINED ENV{MAYA_LOCATION} AND NOT "$ENV{MAYA_LOCATION}" STREQUAL "")
    list(APPEND _maya_hints "$ENV{MAYA_LOCATION}")
endif()
if(DEFINED MAYA_DEVKIT_PROBE_PATHS)
    list(APPEND _maya_hints ${MAYA_DEVKIT_PROBE_PATHS})
endif()

# ---------------------------------------------------------------------------
# Locate Maya's headers.  PATH_SUFFIXES covers both "bundled" and
# "separate devkit" layouts (see MayaVersionMatrix.cmake comment).
# ---------------------------------------------------------------------------
find_path(MAYA_INCLUDE_DIR
    NAMES maya/MFn.h
    HINTS ${_maya_hints}
    PATH_SUFFIXES
        include
        devkit/include
        # macOS .app layout
        MacOS
    DOC "Maya devkit include directory (contains maya/MFn.h)"
)

# ---------------------------------------------------------------------------
# Helper: find_library for a single Maya component.  On Windows the
# import libs live under ``<root>/lib``; on Linux under ``<root>/lib``
# (.so); on macOS they are dylibs inside the .app bundle.
# ---------------------------------------------------------------------------
function(_maya_find_library varname libname)
    find_library(${varname}
        NAMES ${libname}
        HINTS ${_maya_hints}
        PATH_SUFFIXES
            lib
            devkit/lib
            Maya.app/Contents/MacOS
        DOC "Maya library ${libname}"
    )
    set(${varname} "${${varname}}" PARENT_SCOPE)
endfunction()

# Requested components default to the Phase 2A set; callers may pass
# a narrower list via find_package(Maya COMPONENTS ...).
if(NOT Maya_FIND_COMPONENTS)
    set(Maya_FIND_COMPONENTS OpenMaya OpenMayaAnim Foundation)
endif()

set(_maya_component_libs "")
foreach(_comp IN LISTS Maya_FIND_COMPONENTS)
    _maya_find_library(MAYA_${_comp}_LIBRARY "${_comp}")
    # HANDLE_COMPONENTS in find_package_handle_standard_args inspects
    # ``Maya_<comp>_FOUND`` (case-preserved on the component token).
    if(MAYA_${_comp}_LIBRARY)
        set(Maya_${_comp}_FOUND TRUE)
        list(APPEND _maya_component_libs "MAYA_${_comp}_LIBRARY")
    else()
        set(Maya_${_comp}_FOUND FALSE)
    endif()
endforeach()

# OpenMaya is the only strictly-required component for a node plugin;
# OpenMayaAnim / Foundation are PATH_SUFFIX-adjacent and rarely absent,
# but we let find_package_handle_standard_args handle the final verdict.
find_package_handle_standard_args(Maya
    REQUIRED_VARS MAYA_INCLUDE_DIR MAYA_OpenMaya_LIBRARY
    HANDLE_COMPONENTS
)

if(NOT Maya_FOUND)
    if(NOT Maya_FIND_QUIETLY)
        message(STATUS
            "FindMaya: no usable Maya install detected. "
            "Supply -DMAYA_DEVKIT_ROOT=<path to Maya${MAYA_VERSION} install "
            "or devkit> to point the build at your devkit, or export "
            "MAYA_DEVKIT_ROOT / MAYA_LOCATION.")
    endif()
    return()
endif()

# ---------------------------------------------------------------------------
# Export IMPORTED targets (one per component + aggregate).
# SYSTEM include directory shields the project from Maya header warnings.
# ---------------------------------------------------------------------------
foreach(_comp IN LISTS Maya_FIND_COMPONENTS)
    if(MAYA_${_comp}_LIBRARY AND NOT TARGET Maya::${_comp})
        add_library(Maya::${_comp} UNKNOWN IMPORTED)
        set_target_properties(Maya::${_comp} PROPERTIES
            IMPORTED_LOCATION "${MAYA_${_comp}_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MAYA_INCLUDE_DIR}"
            # SYSTEM include → consumers never see Maya header warnings.
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${MAYA_INCLUDE_DIR}"
        )
    endif()
endforeach()

if(NOT TARGET Maya::Maya)
    add_library(Maya::Maya INTERFACE IMPORTED)
    foreach(_comp IN LISTS Maya_FIND_COMPONENTS)
        if(TARGET Maya::${_comp})
            set_property(TARGET Maya::Maya APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES Maya::${_comp})
        endif()
    endforeach()
endif()

mark_as_advanced(MAYA_INCLUDE_DIR)
foreach(_comp IN LISTS Maya_FIND_COMPONENTS)
    mark_as_advanced(MAYA_${_comp}_LIBRARY)
endforeach()
