// =============================================================================
// maya_node/src/mrbf_shape.cpp — Phase 2B Slice 13 (Path B)
// -----------------------------------------------------------------------------
// See mrbf_shape.hpp for the Path A→B retrospective and contract.
//
// Strict attribute-set discipline: this node MUST NOT introduce any
// MFnTypedAttribute of MFnData::kString.  The empirical Path A failure
// (DEVLOG Slice 13) showed that a locator node carrying such an
// attribute triggers Maya's "Unexpected Internal Failure" at
// registerNode time.  Future slices (14 heatmap / 15 X-Ray) must keep
// this invariant — use MFnNumericAttribute or MFnEnumAttribute for
// configuration; leave strings on mRBFNode.
// =============================================================================
#include "rbfmax/maya/mrbf_shape.hpp"

#include <maya/MFnEnumAttribute.h>     // Slice 14 — aHeatmapMode
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MPoint.h>

namespace rbfmax {
namespace maya {

const MString mRBFShape::kTypeName{"mRBFShape"};
const MTypeId mRBFShape::kTypeId{0x00013A01};

MObject mRBFShape::aSourceNode;
MObject mRBFShape::aDrawEnabled;
MObject mRBFShape::aSphereRadius;
MObject mRBFShape::aHeatmapMode;       // Slice 14 — HM-1
MObject mRBFShape::aGridResolution;    // Slice 15 — HM-2
MObject mRBFShape::aGridExtent;        // Slice 15 — HM-2
MObject mRBFShape::aGridZ;             // Slice 15 — HM-2
MObject mRBFShape::aXRayMode;          // Slice 15 — XR-1

mRBFShape::mRBFShape()  = default;
mRBFShape::~mRBFShape() = default;

void* mRBFShape::creator() {
    return new mRBFShape();
}

bool mRBFShape::isBounded() const {
    return true;
}

MBoundingBox mRBFShape::boundingBox() const {
    // Coarse fixed bbox; see hpp for the Slice 14+ tightening note.
    return MBoundingBox(MPoint(-10.0, -10.0, -10.0),
                        MPoint( 10.0,  10.0,  10.0));
}

MStatus mRBFShape::initialize() {
    MStatus st;

    // ---- sourceNode (message) ----------------------------------------
    MFnMessageAttribute mAttr;
    aSourceNode = mAttr.create("sourceNode", "src", &st);
    if (!st) return st;
    mAttr.setStorable(true);
    mAttr.setWritable(true);
    mAttr.setReadable(true);
    mAttr.setConnectable(true);
    st = addAttribute(aSourceNode);
    if (!st) return st;

    // ---- drawEnabled (bool, default true) ----------------------------
    MFnNumericAttribute nAttr;
    aDrawEnabled = nAttr.create("drawEnabled", "de",
                                MFnNumericData::kBoolean, 1, &st);
    if (!st) return st;
    nAttr.setStorable(true);
    nAttr.setKeyable(false);
    nAttr.setReadable(true);
    nAttr.setWritable(true);
    st = addAttribute(aDrawEnabled);
    if (!st) return st;

    // ---- sphereRadius (double, default 0.05) -------------------------
    aSphereRadius = nAttr.create("sphereRadius", "sr",
                                 MFnNumericData::kDouble, 0.05, &st);
    if (!st) return st;
    nAttr.setStorable(true);
    nAttr.setKeyable(false);
    nAttr.setReadable(true);
    nAttr.setWritable(true);
    nAttr.setMin(0.001);
    nAttr.setSoftMax(1.0);
    st = addAttribute(aSphereRadius);
    if (!st) return st;

    // ---- heatmapMode (enum, default 0=Off) -- Slice 14 ---------------
    // Field indices MUST match the HeatmapMode enum in
    // rbfmax/maya/color_mapping.hpp (kOff=0 / kCenterWeights=1 /
    // kPredictionField=2).
    MFnEnumAttribute eAttr;
    aHeatmapMode = eAttr.create("heatmapMode", "hm", 0, &st);
    if (!st) return st;
    eAttr.addField("Off",              0);
    eAttr.addField("Center Weights",   1);
    eAttr.addField("Prediction Field", 2);
    eAttr.setStorable(true);
    eAttr.setKeyable(false);
    eAttr.setChannelBox(true);
    st = addAttribute(aHeatmapMode);
    if (!st) return st;

    // ---- Slice 15 — HM-2 grid (resolution / extent / z) --------------
    aGridResolution = nAttr.create("gridResolution", "gr",
                                   MFnNumericData::kInt, 16, &st);
    if (!st) return st;
    nAttr.setStorable(true);
    nAttr.setKeyable(false);
    nAttr.setChannelBox(true);
    nAttr.setMin(2);
    nAttr.setMax(64);
    st = addAttribute(aGridResolution);
    if (!st) return st;

    aGridExtent = nAttr.create("gridExtent", "ge",
                               MFnNumericData::kDouble, 2.0, &st);
    if (!st) return st;
    nAttr.setStorable(true);
    nAttr.setKeyable(false);
    nAttr.setChannelBox(true);
    nAttr.setMin(0.01);
    nAttr.setSoftMax(100.0);
    st = addAttribute(aGridExtent);
    if (!st) return st;

    aGridZ = nAttr.create("gridZ", "gz",
                          MFnNumericData::kDouble, 0.0, &st);
    if (!st) return st;
    nAttr.setStorable(true);
    nAttr.setKeyable(false);
    nAttr.setChannelBox(true);
    st = addAttribute(aGridZ);
    if (!st) return st;

    // ---- Slice 15 — X-Ray (XR-1) -------------------------------------
    aXRayMode = nAttr.create("xrayMode", "xr",
                             MFnNumericData::kBoolean, 0, &st);
    if (!st) return st;
    nAttr.setStorable(true);
    nAttr.setKeyable(false);
    nAttr.setChannelBox(true);
    st = addAttribute(aXRayMode);
    if (!st) return st;

    return MS::kSuccess;
}

}  // namespace maya
}  // namespace rbfmax
