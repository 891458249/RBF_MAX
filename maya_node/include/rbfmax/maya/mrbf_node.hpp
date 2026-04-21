// =============================================================================
// rbfmax/maya/mrbf_node.hpp — Phase 2A Slice 11
// -----------------------------------------------------------------------------
// Maya DG node that loads a Phase 1 RBFInterpolator from a JSON file on
// disk and serves predict() to downstream attributes.
//
// Contract (Slice 11)
// -------------------
//   * Training data is NOT a Maya attribute.  The user trains offline
//     (mayapy / Python / C++ binding / future rbfmaxTrainAndSave MEL
//     command) and saves schema-v1 JSON via rbfmax::io_json; the node
//     only reads that JSON.  Rationale: DG dirty tracking over an
//     N×D array compound attribute is far more expensive than one file
//     read.  See DEVLOG Slice 11 "core architecture decision".
//   * jsonPath empty        ⇒ node falls back to Slice 10A HelloNode
//     semantics on aInputValue/aOutputValue (legacy compatibility).
//   * jsonPath non-empty    ⇒ node load-once + predict per compute.
//                             Reload triggered when path changes OR
//                             reloadTrigger changes.
//   * Scheduling: MPxNode default (not kParallel).  RBFInterpolator is
//     non-thread-safe per its own contract; Phase 2 may upgrade with
//     clone()-per-thread infrastructure.
//   * All attribute writes inside compute() are eigen_assert-guarded on
//     Debug builds; Release trusts the DG.
//
// See maya_node/README.md "Usage" for an end-to-end Python example.
// =============================================================================
#pragma once

#include <memory>
#include <string>

#include <maya/MObject.h>
#include <maya/MPxNode.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

// Phase 1 forward declaration — avoids pulling Eigen into this header.
namespace rbfmax {
class RBFInterpolator;
}  // namespace rbfmax

namespace rbfmax {
namespace maya {

class mRBFNode : public MPxNode {
public:
    mRBFNode();
    ~mRBFNode() override;

    MStatus compute(const MPlug& plug, MDataBlock& data) override;

    static void*   creator();
    static MStatus initialize();

    static const MString kTypeName;
    static const MTypeId kTypeId;

    // ---- Input attributes ------------------------------------------------
    static MObject aJsonPath;          ///< string — path to schema-v1 JSON
    static MObject aReloadTrigger;     ///< int    — bump to force reload
    static MObject aQueryPoint;        ///< typed(MFnDoubleArrayData) input

    // Slice 10A legacy path: when aJsonPath is empty, the node falls
    // back to hello_transform(aInputValue) → aOutputValue so the
    // Slice 10A smoke (and any existing scenes) still work.
    static MObject aInputValue;        ///< double — legacy 10A input
    static MObject aOutputValue;       ///< double — legacy 10A output

    // ---- Output attributes -----------------------------------------------
    static MObject aOutputValues;      ///< typed(MFnDoubleArrayData) output
    static MObject aIsLoaded;          ///< bool   — true after successful load
    static MObject aNCenters;          ///< int    — fit_result_.centers.rows()
    static MObject aDimInput;          ///< int    — fit_result_.centers.cols()
    static MObject aDimOutput;         ///< int    — fit_result_.weights.cols()
    static MObject aKernelType;        ///< string — kernel_type_to_string()
    static MObject aStatusMessage;     ///< string — human-readable last status

private:
    // Owning pointer: RBFInterpolator is move-only (Phase 1 contract).
    // We reset() on failed load / path change and construct a new
    // instance on successful load.
    std::unique_ptr<rbfmax::RBFInterpolator> interp_;

    std::string loaded_path_;
    int         last_reload_trigger_ {-1};
    bool        warned_about_current_path_ {false};

    // Attempts to load the given path into interp_.  On failure, resets
    // interp_ to nullptr and leaves a descriptive message in the status
    // data handle.  Emits a one-shot MGlobal warning per failing path.
    void try_load(const std::string& path,
                  int                trigger,
                  MDataBlock&        data);
};

}  // namespace maya
}  // namespace rbfmax
