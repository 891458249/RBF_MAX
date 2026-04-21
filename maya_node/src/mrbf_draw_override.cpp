// =============================================================================
// maya_node/src/mrbf_draw_override.cpp — Phase 2B Slice 13 (Path B)
// -----------------------------------------------------------------------------
// Implementation.  See mrbf_draw_override.hpp for the contract +
// Path A→B retrospective.
//
// Failure policy: any hop in the shape -> sourceNode -> mRBFNode chain
// that fails to resolve yields a populated-but-empty RbfDrawData
// (is_loaded = false, center_positions empty).  addUIDrawables then
// emits nothing.  This keeps the draw override robust against partial
// scenes (shape without connection, connection to wrong node type,
// mRBFNode with no loaded interpolator) — all valid intermediate
// states during rigging.
// =============================================================================
#include "rbfmax/maya/mrbf_draw_override.hpp"

#include "rbfmax/maya/mrbf_node.hpp"
#include "rbfmax/maya/mrbf_shape.hpp"

#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>

namespace rbfmax {
namespace maya {

// -----------------------------------------------------------------------------
//  Construction / registration
// -----------------------------------------------------------------------------

mRBFDrawOverride::mRBFDrawOverride(const MObject& obj)
    : MHWRender::MPxDrawOverride(obj,
                                 /*callback=*/nullptr,
                                 /*isAlwaysDirty=*/true) {}

MHWRender::MPxDrawOverride* mRBFDrawOverride::creator(const MObject& obj) {
    return new mRBFDrawOverride(obj);
}

MHWRender::DrawAPI mRBFDrawOverride::supportedDrawAPIs() const {
    // Work on OpenGL / OpenGLCore / DirectX11.  MUIDrawManager::sphere
    // is available on every backend MHWRender::kAllDevices covers.
    return MHWRender::kAllDevices;
}

MBoundingBox mRBFDrawOverride::boundingBox(const MDagPath& /*objPath*/,
                                           const MDagPath& /*cameraPath*/) const {
    // Match the shape's own bbox; see mrbf_shape.cpp.
    return MBoundingBox(MPoint(-10.0, -10.0, -10.0),
                        MPoint( 10.0,  10.0,  10.0));
}

// -----------------------------------------------------------------------------
//  prepareForDraw — runs on main thread, populates a new or recycled
//  RbfDrawData by reading shape attrs + upstream mRBFNode state.
// -----------------------------------------------------------------------------

MUserData* mRBFDrawOverride::prepareForDraw(
        const MDagPath&                  objPath,
        const MDagPath&                  /*cameraPath*/,
        const MHWRender::MFrameContext&  /*frameContext*/,
        MUserData*                       oldData) {
    // objPath is the mRBFShape.  Reuse or allocate the RbfDrawData.
    auto* data = dynamic_cast<RbfDrawData*>(oldData);
    if (data == nullptr) {
        delete oldData;
        data = new RbfDrawData();
    }

    // Reset to empty; we populate only if every hop resolves.
    data->is_loaded = false;
    data->center_positions.clear();

    MFnDagNode shapeFn(objPath.node());

    // --- drawEnabled short-circuit -------------------------------------
    {
        MPlug enPlug = shapeFn.findPlug(mRBFShape::aDrawEnabled, true);
        bool enabled = true;
        enPlug.getValue(enabled);
        if (!enabled) {
            return data;
        }
    }

    // --- sourceNode message connection ---------------------------------
    MPlug srcPlug = shapeFn.findPlug(mRBFShape::aSourceNode, true);
    MPlugArray connected;
    srcPlug.connectedTo(connected, /*asDst=*/true, /*asSrc=*/false);
    if (connected.length() == 0) {
        return data;
    }

    // --- Resolve to mRBFNode + read state ------------------------------
    MFnDependencyNode srcFn(connected[0].node());
    mRBFNode* node = dynamic_cast<mRBFNode*>(srcFn.userNode());
    if (node == nullptr || !node->is_loaded()) {
        return data;
    }

    data->is_loaded        = true;
    data->center_positions = node->centers_for_viewport();
    data->color_centers    = MColor(1.0f, 1.0f, 1.0f);

    // --- Per-shape radius override -------------------------------------
    {
        MPlug rPlug = shapeFn.findPlug(mRBFShape::aSphereRadius, true);
        double radius = 0.05;
        rPlug.getValue(radius);
        data->sphere_radius = static_cast<float>(radius);
    }

    return data;
}

// -----------------------------------------------------------------------------
//  addUIDrawables — runs on render thread; issues MUIDrawManager calls.
// -----------------------------------------------------------------------------

void mRBFDrawOverride::addUIDrawables(
        const MDagPath&                  /*objPath*/,
        MHWRender::MUIDrawManager&       drawManager,
        const MHWRender::MFrameContext&  /*frameContext*/,
        const MUserData*                 data) {
    const auto* d = dynamic_cast<const RbfDrawData*>(data);
    if (d == nullptr || !d->is_loaded) {
        return;
    }
    if (d->center_positions.empty()) {
        return;
    }

    drawManager.beginDrawable();
    drawManager.setColor(d->color_centers);
    drawManager.setDepthPriority(5);
    for (const MPoint& p : d->center_positions) {
        drawManager.sphere(p,
                           static_cast<double>(d->sphere_radius),
                           /*filled=*/true);
    }
    drawManager.endDrawable();
}

}  // namespace maya
}  // namespace rbfmax
