// =============================================================================
// rbfmax/maya/rbfmax_train_cmd.hpp — Phase 2A Slice 12
// -----------------------------------------------------------------------------
// MEL / Python command `rbfmaxTrainAndSave` — closes the Slice 11 gap of
// "users cannot train from inside Maya".  Two input modes (mutually
// exclusive):
//
//   1. inline (flag-driven double lists):
//        cmds.rbfmaxTrainAndSave(
//            centers=[x0, y0, x1, y1, ...],
//            targets=[t0, t1, ...],
//            dim=2, outputDim=1,
//            jsonPath="C:/path/out.json",
//            kernel="Gaussian", eps=1.0, polyDegree=-1,
//            **{"lambda": "auto"}, force=True)
//
//   2. csv (path-driven, Maya doubleArray flags are MEL-hostile for
//      large N):
//        cmds.rbfmaxTrainAndSave(
//            centersFile="C:/centers.csv",
//            targetsFile="C:/targets.csv",
//            jsonPath="C:/out.json", ...)
//
// On success, the command returns the output jsonPath string and writes
// the full schema-v1 Phase 1 fit via rbfmax::io_json.  On failure it
// calls MGlobal::displayError with a descriptive message and returns
// MS::kFailure (Python binding raises RuntimeError).
//
// Not undoable (the command writes a file; undo would need to remember
// the previous file contents, out of scope for Slice 12).
// =============================================================================
#pragma once

#include <maya/MArgList.h>
#include <maya/MPxCommand.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MSyntax.h>

namespace rbfmax {
namespace maya {

class RbfmaxTrainCmd : public MPxCommand {
public:
    RbfmaxTrainCmd()           = default;
    ~RbfmaxTrainCmd() override = default;

    MStatus doIt(const MArgList& args) override;
    bool    isUndoable() const override { return false; }

    static void*   creator() { return new RbfmaxTrainCmd(); }
    static MSyntax newSyntax();

    static const MString kCommandName;  // "rbfmaxTrainAndSave"
};

}  // namespace maya
}  // namespace rbfmax
