// =============================================================================
// maya_node/src/plugin_main.cpp — Phase 2A Slice 10A
// -----------------------------------------------------------------------------
// initializePlugin / uninitializePlugin entry points.  Registers the
// Slice 10A mRBFNode skeleton with Maya.
// =============================================================================
#include <maya/MFnPlugin.h>
#include <maya/MStatus.h>

#include "rbfmax/maya/mrbf_node.hpp"
#include "rbfmax/maya/plugin_info.hpp"

// Maya 2022+ wraps MStatus (and other API types) in an inline namespace
// ``Autodesk::Maya::OpenMaya<version>`` for ABI versioning.  Under MSVC
// strict mode, declaring the entry points as ``extern "C"`` with an
// MStatus return type triggers C2732 because the C-linkage decl conflicts
// with the namespaced type.  The devkit convention since 2022 is therefore
// to drop ``extern "C"`` and let Maya's loader find the functions through
// C++-mangled names on dllexport.  Linux / macOS use the default hidden
// visibility + explicit dllexport/visibility attribute.
#if defined(_WIN32)
    #define RBFMAX_PLUGIN_EXPORT __declspec(dllexport)
#else
    #define RBFMAX_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

RBFMAX_PLUGIN_EXPORT MStatus initializePlugin(MObject obj) {
    MFnPlugin plugin(obj,
                     rbfmax::maya::kPluginVendor,
                     rbfmax::maya::kPluginVersion,
                     "Any");
    MStatus status = plugin.registerNode(
        rbfmax::maya::mRBFNode::kTypeName,
        rbfmax::maya::mRBFNode::kTypeId,
        rbfmax::maya::mRBFNode::creator,
        rbfmax::maya::mRBFNode::initialize);
    if (!status) {
        status.perror("registerNode mRBFNode");
    }
    return status;
}

RBFMAX_PLUGIN_EXPORT MStatus uninitializePlugin(MObject obj) {
    MFnPlugin plugin(obj);
    MStatus status = plugin.deregisterNode(rbfmax::maya::mRBFNode::kTypeId);
    if (!status) {
        status.perror("deregisterNode mRBFNode");
    }
    return status;
}
