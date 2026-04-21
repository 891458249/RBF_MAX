// =============================================================================
// maya_node/src/plugin_main.cpp — Phase 2A Slice 12
// -----------------------------------------------------------------------------
// initializePlugin / uninitializePlugin entry points.  Registers:
//   * Slice 10A : mRBFNode skeleton (HelloNode transform)
//   * Slice 11  : mRBFNode real predict via JSON-path load
//   * Slice 12  : rbfmaxTrainAndSave MPxCommand
// =============================================================================
#include <maya/MFnPlugin.h>
#include <maya/MStatus.h>

#include "rbfmax/maya/mrbf_node.hpp"
#include "rbfmax/maya/plugin_info.hpp"
#include "rbfmax/maya/rbfmax_train_cmd.hpp"

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
        return status;
    }

    // Slice 12 — rbfmaxTrainAndSave MPxCommand.
    status = plugin.registerCommand(
        rbfmax::maya::RbfmaxTrainCmd::kCommandName,
        rbfmax::maya::RbfmaxTrainCmd::creator,
        rbfmax::maya::RbfmaxTrainCmd::newSyntax);
    if (!status) {
        status.perror("registerCommand rbfmaxTrainAndSave");
        // Roll back the node registration so loadPlugin is atomic.
        plugin.deregisterNode(rbfmax::maya::mRBFNode::kTypeId);
        return status;
    }

    return status;
}

RBFMAX_PLUGIN_EXPORT MStatus uninitializePlugin(MObject obj) {
    MFnPlugin plugin(obj);

    MStatus status = plugin.deregisterCommand(
        rbfmax::maya::RbfmaxTrainCmd::kCommandName);
    if (!status) {
        status.perror("deregisterCommand rbfmaxTrainAndSave");
        // Keep unwinding so the node also deregisters.
    }

    MStatus nodeStatus = plugin.deregisterNode(
        rbfmax::maya::mRBFNode::kTypeId);
    if (!nodeStatus) {
        nodeStatus.perror("deregisterNode mRBFNode");
        return nodeStatus;
    }
    return status;
}
