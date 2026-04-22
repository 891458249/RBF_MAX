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

    // --- Slice 14 HM-1 — heatmap mode + per-center colors --------------
    // Slice 15 retired the kPredictionField -> kOff fallback that
    // Slice 14 used; HM-2 now activates the prediction-field branch
    // below.
    HeatmapMode mode = HeatmapMode::kOff;
    {
        MPlug hmPlug = shapeFn.findPlug(mRBFShape::aHeatmapMode, true);
        short mode_i = 0;
        hmPlug.getValue(mode_i);
        mode = static_cast<HeatmapMode>(mode_i);
    }
    data->heatmap_mode = mode;

    if (mode == HeatmapMode::kCenterWeights) {
        const ::rbfmax::MatrixX& W = node->weights();
        const std::size_t n = data->center_positions.size();

        const bool cache_valid =
               (data->last_weights_ptr  == static_cast<const void*>(W.data()))
            && (data->last_weights_rows == W.rows())
            && (data->last_weights_cols == W.cols())
            && (data->last_cached_mode  == HeatmapMode::kCenterWeights)
            && (data->center_colors.size() == n);

        if (!cache_valid) {
            data->center_colors.resize(n);
            if (n > 0) {
                compute_center_colors(W, data->center_colors.data(), n);
            }
            data->last_weights_ptr  = static_cast<const void*>(W.data());
            data->last_weights_rows = W.rows();
            data->last_weights_cols = W.cols();
            data->last_cached_mode  = HeatmapMode::kCenterWeights;
        }
    }
    // For kOff we leave center_colors / cache fields untouched — the
    // next switch back to kCenterWeights will see a stale cache key
    // (mode mismatch) and recompute, which is correct.

    // --- Slice 15 HM-2 — prediction-field grid + Slice 15 X-Ray --------
    {
        MPlug xrPlug = shapeFn.findPlug(mRBFShape::aXRayMode, true);
        bool xray = false;
        xrPlug.getValue(xray);
        data->xray_mode = xray;
    }

    if (mode == HeatmapMode::kPredictionField) {
        int    grid_res = 16;
        double grid_ext = 2.0;
        double grid_z   = 0.0;
        shapeFn.findPlug(mRBFShape::aGridResolution, true).getValue(grid_res);
        shapeFn.findPlug(mRBFShape::aGridExtent,     true).getValue(grid_ext);
        shapeFn.findPlug(mRBFShape::aGridZ,          true).getValue(grid_z);

        const ::rbfmax::MatrixX& W   = node->weights();
        const Eigen::Index       dim = node->input_dim();

        const bool hm2_cache_valid =
               (data->last_weights_ptr     == static_cast<const void*>(W.data()))
            && (data->last_weights_rows    == W.rows())
            && (data->last_weights_cols    == W.cols())
            && (data->last_cached_mode     == HeatmapMode::kPredictionField)
            && (data->last_grid_resolution == grid_res)
            && (data->last_grid_extent     == grid_ext)
            && (data->last_grid_z          == grid_z);

        if (!hm2_cache_valid && dim > 0
            && grid_res >= 2 && grid_ext > 0.0) {
            ::rbfmax::MatrixX samples;
            build_grid_sample_points(grid_res, grid_ext, grid_z,
                                     dim, samples);
            const Eigen::Index n_grid = samples.rows();

            if (n_grid > 0) {
                const ::rbfmax::MatrixX preds =
                    node->predict_batch_samples(samples);

                data->grid_positions.resize(static_cast<std::size_t>(n_grid));
                data->grid_colors.resize(static_cast<std::size_t>(n_grid));

                for (Eigen::Index i = 0; i < n_grid; ++i) {
                    const double gx = (samples.cols() >= 1)
                        ? static_cast<double>(samples(i, 0)) : 0.0;
                    const double gy = (samples.cols() >= 2)
                        ? static_cast<double>(samples(i, 1)) : 0.0;
                    const double gz = (samples.cols() >= 3)
                        ? static_cast<double>(samples(i, 2)) : grid_z;
                    data->grid_positions[static_cast<std::size_t>(i)] =
                        MPoint(gx, gy, gz);
                }

                compute_grid_colors(preds, data->grid_colors.data(),
                                    static_cast<std::size_t>(n_grid));

                data->last_weights_ptr     = static_cast<const void*>(W.data());
                data->last_weights_rows    = W.rows();
                data->last_weights_cols    = W.cols();
                data->last_cached_mode     = HeatmapMode::kPredictionField;
                data->last_grid_resolution = grid_res;
                data->last_grid_extent     = grid_ext;
                data->last_grid_z          = grid_z;
            }
        } else if (dim == 0) {
            // No interp dim → clear so addUIDrawables draws nothing
            // for the grid layer (centers still draw).
            data->grid_positions.clear();
            data->grid_colors.clear();
        }
    } else {
        // Mode is not HM-2 — drop the grid so we don't draw stale points
        // when the user toggles back to kOff or kCenterWeights.  Cache
        // key fields stay; the next switch back to HM-2 sees the prior
        // mode mismatch and recomputes correctly.
        data->grid_positions.clear();
        data->grid_colors.clear();
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

    // Slice 15 — X-Ray depth priority.  Maya 2022/2025 expose only the
    // `void setDepthPriority(unsigned int)` signature on MUIDrawManager
    // (no enum), per the A2 pre-flight.  We use raw integers:
    //   5  — Slice 13 default (matches MUIDrawManager dormant priority
    //        used by Autodesk samples)
    //   10 — Slice 15 X-Ray (well above scene geometry; renders on top)
    const unsigned int depth_priority = d->xray_mode ? 10u : 5u;
    drawManager.setDepthPriority(depth_priority);

    // Stage 1 — Slice 15 HM-2 prediction-field grid (background layer).
    // Drawn first so centers (Stage 2) overlay on top.
    if (d->heatmap_mode == HeatmapMode::kPredictionField
        && !d->grid_positions.empty()
        && d->grid_colors.size() == d->grid_positions.size()) {
        for (std::size_t i = 0; i < d->grid_positions.size(); ++i) {
            const auto& c = d->grid_colors[i];
            drawManager.setColor(MColor(c[0], c[1], c[2], c[3]));
            drawManager.sphere(d->grid_positions[i],
                               static_cast<double>(d->grid_sphere_radius),
                               /*filled=*/true);
        }
    }

    // Stage 2 — centers (always drawn when is_loaded).  Color depends
    // on the heatmap mode:
    //   kCenterWeights : Slice 14 per-center viridis
    //   kOff           : Slice 13 uniform white batch
    //   kPredictionField : white (provides contrast against the colored
    //                      grid that Stage 1 just rendered)
    if (d->heatmap_mode == HeatmapMode::kCenterWeights
        && d->center_colors.size() == d->center_positions.size()) {
        for (std::size_t i = 0; i < d->center_positions.size(); ++i) {
            const auto& c = d->center_colors[i];
            drawManager.setColor(MColor(c[0], c[1], c[2], c[3]));
            drawManager.sphere(d->center_positions[i],
                               static_cast<double>(d->sphere_radius),
                               /*filled=*/true);
        }
    } else {
        drawManager.setColor(d->color_centers);
        for (const MPoint& p : d->center_positions) {
            drawManager.sphere(p,
                               static_cast<double>(d->sphere_radius),
                               /*filled=*/true);
        }
    }

    drawManager.endDrawable();
}

}  // namespace maya
}  // namespace rbfmax
