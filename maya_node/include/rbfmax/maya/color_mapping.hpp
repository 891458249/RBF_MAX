// =============================================================================
// rbfmax/maya/color_mapping.hpp — Phase 2B Slice 14 (HM-1)
// -----------------------------------------------------------------------------
// Pure-C++14 viridis-approximated color mapping for the heatmap modes
// of mRBFShape's Viewport 2.0 draw override.  Maya-free; this header
// can be (and is) compiled into the adapter test target without the
// Maya devkit.
//
// HeatmapMode values are kept in lockstep with the enum field indices
// added to mRBFShape::aHeatmapMode.  Any addition / reorder MUST be
// applied in both this header and mrbf_shape.cpp's initialize().
// =============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Eigen/Core>
#include <rbfmax/types.hpp>

namespace rbfmax {
namespace maya {

// Heatmap display mode on mRBFShape.  Underlying short values match
// the field indices passed to MFnEnumAttribute::addField.
enum class HeatmapMode : std::int16_t {
    kOff             = 0,   // Slice 13 default (uniform white spheres)
    kCenterWeights   = 1,   // Slice 14 HM-1 (per-center viridis coloring)
    kPredictionField = 2,   // Slice 15 placeholder — currently falls back to kOff
};

// Map a [0, 1] scalar to a viridis-approximated RGBA color.
// Input is clamped to [0, 1].  Alpha is always 1.0.
// Precision: ~0.01 per channel vs matplotlib's 256-entry LUT (validated
// at v=0.0/0.5/1.0 in test_color_mapping.cpp F1-F3).
std::array<float, 4> map_scalar_to_color(Scalar v01) noexcept;

// Compute per-center RGBA colors from a weights matrix (N × M).
//
//   out_colors: caller-allocated buffer of at least n_centers elements.
//   n_centers:  must equal weights.rows(); extra entries tolerated.
//
// Algorithm:
//   heat[i] = weights.row(i).norm()                  // L2 across M dims
//   v01[i]  = (heat[i] - min_h) / (max_h - min_h + 1e-12)
//   out_colors[i] = map_scalar_to_color(v01[i])
//
// Degenerate handling:
//   - weights empty (0 rows)  → no writes
//   - all weights equal        → all colors = map_scalar_to_color(0.0)
//   - NaN / Inf in any weight  → that center falls back to (1, 1, 1, 1)
void compute_center_colors(
    const MatrixX&            weights,
    std::array<float, 4>*     out_colors,
    std::size_t               n_centers) noexcept;

}  // namespace maya
}  // namespace rbfmax
