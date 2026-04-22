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
#include <vector>                // Slice 13 — centers_for_viewport()

#include <maya/MObject.h>
#include <maya/MPoint.h>         // Slice 13 — centers_for_viewport()
#include <maya/MPxNode.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

#include <rbfmax/types.hpp>      // Slice 14 — MatrixX for weights()

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

    // ---- Slice 13 Viewport 2.0 accessors (read-only, additive) ----------
    //
    // These are used by mRBFDrawOverride (which is registered on the
    // separate mRBFShape locator node) to populate its RbfDrawData.
    // Path B architecture: mRBFNode stays a pure kDependNode; the
    // mRBFShape locator reads from here over a message connection.
    //
    // is_loaded() — true iff a fit_result_ is available via interp_.
    bool is_loaded() const noexcept;

    // centers_for_viewport() — returns a vector of the fit_result_
    // centers projected into the first 3 dimensions.  D<3 is zero-
    // padded; D>3 is truncated.  Returns an empty vector if
    // !is_loaded().  Always a fresh copy — safe to call from the
    // draw thread.
    std::vector<MPoint> centers_for_viewport() const;

    // Slice 14 (HM-1) — direct read access to the interpolator's
    // weights matrix (N × M).  Returns a default-constructed 0×0
    // matrix when !is_loaded().  Caller must NOT mutate.  The
    // returned reference is invalidated on the next try_load() that
    // resets interp_; the DrawOverride never holds the reference
    // across frames (it copies the buffer pointer + shape into
    // RbfDrawData's cache key only).
    const ::rbfmax::MatrixX& weights() const noexcept;

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
