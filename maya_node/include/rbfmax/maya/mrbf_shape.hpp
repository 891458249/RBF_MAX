// =============================================================================
// rbfmax/maya/mrbf_shape.hpp — Phase 2B Slice 13 (Path B)
// -----------------------------------------------------------------------------
// A minimal MPxLocatorNode whose sole purpose is to host the
// mRBFDrawOverride in Viewport 2.0.  Path B retrospective:
//
//   Slice 13 originally attempted to reparent mRBFNode itself to
//   MPxLocatorNode and register it with the 6-arg registerNode + a
//   drawdb classification.  Registration consistently returned
//   "Unexpected Internal Failure" on both Maya 2022 and 2025 under
//   every combination tested (see DEVLOG Slice 13 "Path A failure
//   analysis").  Empirical finding: a locator node carrying even a
//   single MFnTypedAttribute of MFnData::kString triggers the
//   internal failure.
//
//   Path B sidesteps this by keeping mRBFNode as a pure kDependNode
//   (Phase 2A unchanged) and introducing this separate locator whose
//   attribute set is strictly numeric + message — no kString typed
//   attributes.  The Viewport 2.0 draw override is registered on
//   this node's classification.  Data flows:
//
//       mRBFNode.message ─► mRBFShape.sourceNode
//
//   on which mRBFDrawOverride's prepareForDraw upstreams to read the
//   node's is_loaded() / centers_for_viewport() accessors.
//
// typeId 0x00013A01 — dev range, immediately after mRBFNode's
// 0x00013A00.  Self-check: 0x00013A01 = 80385 (< 0x7FFFF = 524287).
// =============================================================================
#pragma once

#include <maya/MBoundingBox.h>
#include <maya/MObject.h>
#include <maya/MPxLocatorNode.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

namespace rbfmax {
namespace maya {

class mRBFShape : public MPxLocatorNode {
public:
    mRBFShape();
    ~mRBFShape() override;

    // MPxLocatorNode overrides -----------------------------------------
    // Slice 13 uses a generous fixed bbox (-10..10) so framing /
    // frustum culling never drops the locator on first selection.
    // Slice 14+ may tighten this around the connected node's centers
    // extent once the heatmap mode is landed.
    bool         isBounded()   const override;
    MBoundingBox boundingBox() const override;

    // Registration ------------------------------------------------------
    static void*   creator();
    static MStatus initialize();

    static const MString kTypeName;      ///< "mRBFShape"
    static const MTypeId kTypeId;        ///< 0x00013A01

    // Attributes --------------------------------------------------------
    // sourceNode (message, writable) — the mRBFNode whose centers this
    // shape visualizes.  A typical user binding is:
    //     connectAttr mRBFNode.message mRBFShape.sourceNode
    static MObject aSourceNode;

    // drawEnabled (bool, default true) — per-shape toggle.  When
    // false, prepareForDraw returns an empty RbfDrawData so
    // addUIDrawables emits nothing.
    static MObject aDrawEnabled;

    // sphereRadius (double, default 0.05) — per-shape radius for the
    // center markers.  Range clamped [0.001, soft 1.0] via
    // setMin / setSoftMax in initialize().
    static MObject aSphereRadius;

    // heatmapMode (enum, default 0=Off) — Slice 14 HM-1.
    //   0 = Off               (uniform white spheres, Slice 13 default)
    //   1 = Center Weights    (per-center viridis from row-L2 weight norm)
    //   2 = Prediction Field  (Slice 15 — viridis grid via predict_batch)
    // Field indices MUST match the HeatmapMode enum in
    // rbfmax/maya/color_mapping.hpp.
    static MObject aHeatmapMode;

    // Slice 15 — HM-2 (prediction field) + X-Ray (XR-1).
    //
    // gridResolution (int, default 16, range [2, 64]) — points per side
    // of the sample grid; total samples = G^2.  Capped at 64 in attribute
    // metadata (channel-box slider); the underlying color_mapping sanity
    // cap is 256, so out-of-range values from setAttr are clamped at the
    // adapter layer too.
    static MObject aGridResolution;

    // gridExtent (double, default 2.0, min 0.01, softMax 100.0) — half-
    // width of the XY sample area in local space.  Grid covers
    // [-gridExtent, +gridExtent] x [-gridExtent, +gridExtent].
    static MObject aGridExtent;

    // gridZ (double, default 0.0, no clamp) — Z-plane height for D >= 3
    // input dimensions.  Ignored for D <= 2 input.
    static MObject aGridZ;

    // xrayMode (bool, default false) — when true, raise the
    // MUIDrawManager depth priority so centers + grid render on top of
    // scene geometry.  See mrbf_draw_override.cpp for the priority
    // values (raw integers — Maya 2022/2025 expose no DepthPriority enum
    // on MUIDrawManager).
    static MObject aXRayMode;
};

}  // namespace maya
}  // namespace rbfmax
