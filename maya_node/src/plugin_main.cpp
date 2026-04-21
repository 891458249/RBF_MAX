// =============================================================================
// maya_node/src/plugin_main.cpp — Phase 2B Slice 13 (Path B)
// -----------------------------------------------------------------------------
// initializePlugin / uninitializePlugin entry points.  Registers:
//   * Slice 10A : mRBFNode skeleton (HelloNode transform) — kDependNode
//   * Slice 11  : mRBFNode real predict via JSON-path load
//   * Slice 12  : rbfmaxTrainAndSave MPxCommand
//   * Slice 13  : mRBFShape (kLocatorNode + classification)
//                 + mRBFDrawOverride registered against that
//                 classification via MDrawRegistry.
//
// Path B note
// -----------
// mRBFNode STAYS a kDependNode (4-arg registerNode) — Phase 2A
// untouched.  Viewport 2.0 integration is carried entirely by the
// auxiliary mRBFShape locator; see mrbf_shape.hpp and DEVLOG
// Slice 13 "Path A→B retrospective" for the architectural pivot.
// =============================================================================
#include <maya/MDrawRegistry.h>   // Slice 13 — draw override registration
#include <maya/MFnPlugin.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

#include "rbfmax/maya/mrbf_draw_override.hpp"  // Slice 13 — creator + classification
#include "rbfmax/maya/mrbf_node.hpp"
#include "rbfmax/maya/mrbf_shape.hpp"          // Slice 13 — locator host
#include "rbfmax/maya/plugin_info.hpp"
#include "rbfmax/maya/rbfmax_train_cmd.hpp"

// File-scope MStrings for classification / registrant (Path B on
// mRBFShape).  The MString must outlive the registerNode call and the
// plugin's lifetime because Maya stores its address, not a copy.
// Matches Autodesk cvColor / footPrint sample pattern.
namespace {
const MString gDrawClassificationMString(rbfmax::maya::kDrawClassification);
const MString gDrawRegistrantIdMString(rbfmax::maya::kDrawRegistrantId);
}  // namespace

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

    // Slice 13 — mRBFShape locator (6-arg registerNode with
    // kLocatorNode + classification pointer).  This is the Path B
    // replacement for reparenting mRBFNode itself; see DEVLOG.
    status = plugin.registerNode(
        rbfmax::maya::mRBFShape::kTypeName,
        rbfmax::maya::mRBFShape::kTypeId,
        rbfmax::maya::mRBFShape::creator,
        rbfmax::maya::mRBFShape::initialize,
        MPxNode::kLocatorNode,
        &gDrawClassificationMString);
    if (!status) {
        status.perror("registerNode mRBFShape");
        plugin.deregisterCommand(rbfmax::maya::RbfmaxTrainCmd::kCommandName);
        plugin.deregisterNode(rbfmax::maya::mRBFNode::kTypeId);
        return status;
    }

    // Slice 13 — mRBFDrawOverride creator, keyed on the mRBFShape
    // classification.  Done AFTER registerNode so the classification
    // string is known to the draw registry by the time any DG refresh
    // requests a draw.
    status = MHWRender::MDrawRegistry::registerDrawOverrideCreator(
        gDrawClassificationMString,
        gDrawRegistrantIdMString,
        rbfmax::maya::mRBFDrawOverride::creator);
    if (!status) {
        status.perror("registerDrawOverrideCreator mRBFDrawOverride");
        plugin.deregisterNode(rbfmax::maya::mRBFShape::kTypeId);
        plugin.deregisterCommand(rbfmax::maya::RbfmaxTrainCmd::kCommandName);
        plugin.deregisterNode(rbfmax::maya::mRBFNode::kTypeId);
        return status;
    }

    return status;
}

RBFMAX_PLUGIN_EXPORT MStatus uninitializePlugin(MObject obj) {
    MFnPlugin plugin(obj);

    // Slice 13 — unwind in reverse registration order.

    MStatus drawStatus = MHWRender::MDrawRegistry::deregisterDrawOverrideCreator(
        gDrawClassificationMString, gDrawRegistrantIdMString);
    if (!drawStatus) {
        drawStatus.perror("deregisterDrawOverrideCreator mRBFDrawOverride");
        // Keep unwinding.
    }

    MStatus shapeStatus = plugin.deregisterNode(
        rbfmax::maya::mRBFShape::kTypeId);
    if (!shapeStatus) {
        shapeStatus.perror("deregisterNode mRBFShape");
        // Keep unwinding.
    }

    MStatus cmdStatus = plugin.deregisterCommand(
        rbfmax::maya::RbfmaxTrainCmd::kCommandName);
    if (!cmdStatus) {
        cmdStatus.perror("deregisterCommand rbfmaxTrainAndSave");
        // Keep unwinding.
    }

    MStatus nodeStatus = plugin.deregisterNode(
        rbfmax::maya::mRBFNode::kTypeId);
    if (!nodeStatus) {
        nodeStatus.perror("deregisterNode mRBFNode");
        return nodeStatus;
    }

    // Surface the first non-kSuccess from the earlier steps; Maya's
    // plugin loader logs this without failing the unload.
    if (!drawStatus)  return drawStatus;
    if (!shapeStatus) return shapeStatus;
    if (!cmdStatus)   return cmdStatus;
    return MS::kSuccess;
}
