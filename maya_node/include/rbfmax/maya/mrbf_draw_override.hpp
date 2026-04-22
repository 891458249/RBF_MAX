// =============================================================================
// rbfmax/maya/mrbf_draw_override.hpp — Phase 2B Slice 13 (Path B)
// -----------------------------------------------------------------------------
// Viewport 2.0 draw override registered on mRBFShape (not mRBFNode —
// see DEVLOG Slice 13 "Path A→B retrospective" for the architectural
// pivot reasoning).  Slice 13 scope: render each trained center of
// the upstream mRBFNode as a white filled sphere.  Slices 14/15 will
// layer heatmap colouring and X-ray depth priority on top.
//
// Architectural contract
// ----------------------
//   * kDrawClassification MUST match the string passed to
//     MFnPlugin::registerNode's 6th argument for mRBFShape in
//     plugin_main.cpp.  Smoke smoke_viewport.py asserts
//     `getClassification("mRBFShape")` contains this literal so any
//     future drift is caught at plugin-load time.
//   * prepareForDraw reads the mRBFNode state through a two-hop
//     traversal: shape.sourceNode (message plug) -> upstream
//     dependency node -> dynamic_cast to mRBFNode -> additive
//     is_loaded() / centers_for_viewport() accessors (mrbf_node.hpp).
//     Any failure along this chain yields an empty RbfDrawData (no
//     draw) — never crash, never warn.
//
// References: Maya 2022 and 2025 devkit headers MPxDrawOverride.h,
// MDrawRegistry.h, MUIDrawManager.h have matching signatures for
// the overrides used here (grep-verified pre-write).
// =============================================================================
#pragma once

#include <array>
#include <vector>

#include <Eigen/Core>                       // Eigen::Index for cache key

#include <maya/MBoundingBox.h>
#include <maya/MColor.h>
#include <maya/MDagPath.h>
#include <maya/MFrameContext.h>
#include <maya/MObject.h>
#include <maya/MPoint.h>
#include <maya/MPxDrawOverride.h>
#include <maya/MUIDrawManager.h>
#include <maya/MUserData.h>

#include "rbfmax/maya/color_mapping.hpp"   // Slice 14 — HeatmapMode

namespace rbfmax {
namespace maya {

// Draw-classification / registrant string literals — the ONLY source
// of truth.  plugin_main.cpp wraps these into MString and passes them
// to MFnPlugin::registerNode (6th arg, for mRBFShape) and
// MDrawRegistry::registerDrawOverrideCreator.
//
// We use the 3-level "drawdb/geometry/rbfmax/mRBFShape" layout.  Path A
// abandoned 3-level on mRBFNode because of a separate failure mode on
// that specific type (see DEVLOG), but 3-level is accepted on a
// locator-only-from-creation node.  If any registration ever rejects
// this, fall back to 2-level "drawdb/geometry/mRBFShape".
constexpr const char* kDrawClassification = "drawdb/geometry/rbfmax/mRBFShape";
constexpr const char* kDrawRegistrantId   = "RBFMaxPlugin";

// MUserData subclass — prepareForDraw produces, addUIDrawables consumes.
// Members are public so the DrawOverride can populate them directly
// without setter boilerplate; the object is short-lived and owned by
// Maya so encapsulation would add cost without value.
class RbfDrawData : public MUserData {
public:
    RbfDrawData()           = default;
    ~RbfDrawData() override = default;

    bool                is_loaded      = false;
    std::vector<MPoint> center_positions;
    MColor              color_centers  = MColor(1.0f, 1.0f, 1.0f);  // kOff
    float               sphere_radius  = 0.05f;

    // Slice 14 — per-center colors (used when heatmap_mode != kOff).
    // Size invariant when populated: matches center_positions.size().
    std::vector<std::array<float, 4>> center_colors;
    HeatmapMode         heatmap_mode   = HeatmapMode::kOff;

    // Cache key for skipping redundant compute_center_colors() work.
    // We compare the underlying weights buffer pointer + shape; if all
    // three match the prior frame and the mode is unchanged, the cached
    // center_colors are reused as-is.  This pointer is observed only,
    // never dereferenced after the comparison.
    const void*   last_weights_ptr  = nullptr;
    Eigen::Index  last_weights_rows = 0;
    Eigen::Index  last_weights_cols = 0;
    HeatmapMode   last_cached_mode  = HeatmapMode::kOff;

    // Slice 15 — HM-2 prediction-field grid.
    // Filled by prepareForDraw when heatmap_mode == kPredictionField;
    // consumed by addUIDrawables.  Empty in other modes.
    std::vector<MPoint>                grid_positions;
    std::vector<std::array<float, 4>>  grid_colors;
    float                              grid_sphere_radius = 0.015f;

    // Cache key extension for HM-2 — beyond the weights buffer key,
    // the grid also depends on resolution / extent / z.  Any of these
    // changing invalidates the grid cache.
    int     last_grid_resolution = 0;
    double  last_grid_extent     = 0.0;
    double  last_grid_z          = 0.0;

    // Slice 15 — XR-1 X-Ray mode flag (read each frame from the shape's
    // xrayMode plug; consumed by addUIDrawables to bump depth priority).
    bool    xray_mode            = false;
};

class mRBFDrawOverride : public MHWRender::MPxDrawOverride {
public:
    static MHWRender::MPxDrawOverride* creator(const MObject& obj);

    MHWRender::DrawAPI supportedDrawAPIs() const override;

    bool hasUIDrawables() const override { return true; }
    bool isBounded(const MDagPath&, const MDagPath&) const override { return false; }

    MBoundingBox boundingBox(const MDagPath& objPath,
                             const MDagPath& cameraPath) const override;

    MUserData* prepareForDraw(const MDagPath&                  objPath,
                              const MDagPath&                  cameraPath,
                              const MHWRender::MFrameContext&  frameContext,
                              MUserData*                       oldData) override;

    void addUIDrawables(const MDagPath&                  objPath,
                        MHWRender::MUIDrawManager&       drawManager,
                        const MHWRender::MFrameContext&  frameContext,
                        const MUserData*                 data) override;

private:
    explicit mRBFDrawOverride(const MObject& obj);
};

}  // namespace maya
}  // namespace rbfmax
