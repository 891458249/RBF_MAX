// =============================================================================
// rbfmax/maya/mrbf_node.hpp — Phase 2A Slice 10A
// -----------------------------------------------------------------------------
// Skeleton DG node that routes a single double input through the Phase 1
// Gaussian kernel and writes the result to a single double output.
//
// Slice 10A contract — validate-only:
//   * Proves the CMake / FindMaya pipeline lights up rbfmax::kernel
//     inside a Maya translation unit.
//   * Proves MFnPlugin registration + typeId choice + attribute wiring
//     survive a mayapy loadPlugin/createNode/setAttr/unloadPlugin cycle.
//   * Intentionally does NOT expose RBF fit/predict yet — that lands in
//     Slice 11 where the node grows dynamic array attributes and real
//     solver state.
// =============================================================================
#pragma once

#include <maya/MObject.h>
#include <maya/MPxNode.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

namespace rbfmax {
namespace maya {

class mRBFNode : public MPxNode {
public:
    mRBFNode()           = default;
    ~mRBFNode() override = default;

    // MPxNode override — writes hello_transform(input) to output.
    MStatus compute(const MPlug& plug, MDataBlock& data) override;

    // Registration helpers used by plugin_main.cpp.
    static void*   creator();
    static MStatus initialize();

    // Identity.  kTypeId derives its raw value from plugin_info.hpp's
    // kNodeTypeIdValue (configured at CMake time).
    static const MString kTypeName;
    static const MTypeId kTypeId;

    // Attributes.  Two scalar doubles in Slice 10A.
    static MObject aInputValue;
    static MObject aOutputValue;
};

}  // namespace maya
}  // namespace rbfmax
