// =============================================================================
// maya_node/src/mrbf_node.cpp — Phase 2A Slice 10A
// -----------------------------------------------------------------------------
// Implementation of the Slice 10A HelloNode skeleton.  See mrbf_node.hpp
// for the contract; see adapter_core.hpp for the arithmetic.
// =============================================================================
#include "rbfmax/maya/mrbf_node.hpp"

#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>

#include "rbfmax/maya/adapter_core.hpp"
#include "rbfmax/maya/plugin_info.hpp"

namespace rbfmax {
namespace maya {

// -----------------------------------------------------------------------------
//  Static member definitions
// -----------------------------------------------------------------------------

const MString mRBFNode::kTypeName{"mRBFNode"};
const MTypeId mRBFNode::kTypeId{kNodeTypeIdValue};

MObject mRBFNode::aInputValue;
MObject mRBFNode::aOutputValue;

// -----------------------------------------------------------------------------
//  Factory
// -----------------------------------------------------------------------------

void* mRBFNode::creator() {
    return new mRBFNode();
}

// -----------------------------------------------------------------------------
//  Attribute wiring
// -----------------------------------------------------------------------------

MStatus mRBFNode::initialize() {
    MStatus status;
    MFnNumericAttribute nAttr;

    aInputValue = nAttr.create(
        "inputValue", "iv", MFnNumericData::kDouble, 0.0, &status);
    if (!status) {
        return status;
    }
    nAttr.setKeyable(true);
    nAttr.setStorable(true);
    nAttr.setReadable(true);
    nAttr.setWritable(true);

    aOutputValue = nAttr.create(
        "outputValue", "ov", MFnNumericData::kDouble, 0.0, &status);
    if (!status) {
        return status;
    }
    nAttr.setKeyable(false);
    nAttr.setStorable(false);
    nAttr.setReadable(true);
    nAttr.setWritable(false);

    status = addAttribute(aInputValue);
    if (!status) return status;
    status = addAttribute(aOutputValue);
    if (!status) return status;

    status = attributeAffects(aInputValue, aOutputValue);
    if (!status) return status;

    return MS::kSuccess;
}

// -----------------------------------------------------------------------------
//  Compute
// -----------------------------------------------------------------------------

MStatus mRBFNode::compute(const MPlug& plug, MDataBlock& data) {
    // Slice 10A has exactly one output plug to service.
    if (plug != aOutputValue) {
        return MS::kUnknownParameter;
    }

    MStatus status;
    MDataHandle inHandle = data.inputValue(aInputValue, &status);
    if (!status) {
        return status;
    }
    const double x = inHandle.asDouble();

    // D7 — the Slice 10A transform is the Phase 1 Gaussian kernel.
    const double y = static_cast<double>(
        rbfmax::maya::hello_transform(static_cast<rbfmax::Scalar>(x)));

    MDataHandle outHandle = data.outputValue(aOutputValue, &status);
    if (!status) {
        return status;
    }
    outHandle.setDouble(y);
    data.setClean(plug);
    return MS::kSuccess;
}

}  // namespace maya
}  // namespace rbfmax
