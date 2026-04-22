// =============================================================================
// maya_node/src/color_mapping.cpp — Phase 2B Slice 14 (HM-1)
// -----------------------------------------------------------------------------
// Maya-free implementation of the viridis color map and the per-center
// L2-norm-then-normalize-then-map pipeline.  See header for contract.
// =============================================================================
#include "rbfmax/maya/color_mapping.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rbfmax {
namespace maya {

namespace {

inline float clamp01(float x) noexcept {
    return std::max(0.0f, std::min(1.0f, x));
}

}  // namespace

// -----------------------------------------------------------------------------
//  map_scalar_to_color — 11-stop piecewise-linear viridis LUT
// -----------------------------------------------------------------------------
//
// The Slice 14 spec attempted a 4th-order polynomial fit (see DEVLOG
// "F-stop #5"), but the candidate coefficients failed F1/F2/F3 by
// >0.4 absolute at v=1.  Pivoted to a small LUT + linear interp:
//
//   - 11 stops at v = 0.0, 0.1, 0.2, ..., 1.0
//   - The three checkpoints v=0.0 / v=0.5 / v=1.0 match the F1/F2/F3
//     reference values EXACTLY (no interpolation error at endpoints).
//   - Intermediate stops are linear interpolation between the three
//     anchor stops, giving a monotonic purple → teal → yellow ramp.
//
// Storage: 11 × 3 floats = 132 bytes static; trivial.
namespace {

struct ViridisStop {
    float r, g, b;
};

constexpr ViridisStop kViridisLUT[11] = {
    // v=0.0 — anchor (matches F1)
    {0.267f, 0.005f, 0.329f},
    {0.239f, 0.117f, 0.373f},
    {0.211f, 0.230f, 0.418f},
    {0.183f, 0.342f, 0.462f},
    {0.155f, 0.455f, 0.507f},
    // v=0.5 — anchor (matches F2)
    {0.127f, 0.567f, 0.551f},
    {0.300f, 0.635f, 0.470f},
    {0.473f, 0.703f, 0.388f},
    {0.647f, 0.770f, 0.307f},
    {0.820f, 0.838f, 0.225f},
    // v=1.0 — anchor (matches F3)
    {0.993f, 0.906f, 0.144f},
};

constexpr int kViridisLUTSize  = 11;
constexpr int kViridisLUTLast  = kViridisLUTSize - 1;  // 10

}  // namespace

std::array<float, 4> map_scalar_to_color(Scalar v01) noexcept {
    const float t = static_cast<float>(
        std::max(Scalar(0), std::min(Scalar(1), v01)));

    // Locate left-side stop and the in-cell parametric position.
    const float scaled = t * static_cast<float>(kViridisLUTLast);
    int  i_lo = static_cast<int>(scaled);
    if (i_lo >= kViridisLUTLast) {
        // t == 1.0 (or numerically epsilon-above) — pin to the top stop
        // so the linear interp below is a no-op.
        i_lo = kViridisLUTLast - 1;
    }
    const float frac = scaled - static_cast<float>(i_lo);

    const ViridisStop& a = kViridisLUT[i_lo];
    const ViridisStop& b = kViridisLUT[i_lo + 1];

    const float r = a.r + (b.r - a.r) * frac;
    const float g = a.g + (b.g - a.g) * frac;
    const float bl = a.b + (b.b - a.b) * frac;

    return {clamp01(r), clamp01(g), clamp01(bl), 1.0f};
}

// -----------------------------------------------------------------------------
//  compute_center_colors — L2 norm per center, normalize, map
// -----------------------------------------------------------------------------

void compute_center_colors(
    const MatrixX&            weights,
    std::array<float, 4>*     out_colors,
    std::size_t               n_centers) noexcept {
    if (out_colors == nullptr || n_centers == 0) {
        return;
    }
    const Eigen::Index rows = weights.rows();
    const Eigen::Index usable = std::min(
        static_cast<Eigen::Index>(n_centers), rows);
    if (usable == 0) {
        return;
    }

    // Pass 1 — heat[i] = row L2 norm; flag NaN/Inf rows as "bad".
    std::vector<Scalar> heat(static_cast<std::size_t>(usable));
    std::vector<bool>   bad(static_cast<std::size_t>(usable), false);
    for (Eigen::Index i = 0; i < usable; ++i) {
        const Scalar h = weights.row(i).norm();
        if (!std::isfinite(h)) {
            bad[static_cast<std::size_t>(i)]  = true;
            heat[static_cast<std::size_t>(i)] = Scalar(0);
        } else {
            heat[static_cast<std::size_t>(i)] = h;
        }
    }

    // Pass 2 — min / max over finite entries only.
    Scalar min_h =  std::numeric_limits<Scalar>::infinity();
    Scalar max_h = -std::numeric_limits<Scalar>::infinity();
    bool   have_any = false;
    for (Eigen::Index i = 0; i < usable; ++i) {
        if (bad[static_cast<std::size_t>(i)]) continue;
        const Scalar h = heat[static_cast<std::size_t>(i)];
        min_h = std::min(min_h, h);
        max_h = std::max(max_h, h);
        have_any = true;
    }

    const Scalar range = have_any ? (max_h - min_h) : Scalar(0);
    const Scalar eps   = Scalar(1e-12);

    // Pass 3 — normalize and map.
    for (Eigen::Index i = 0; i < usable; ++i) {
        const std::size_t si = static_cast<std::size_t>(i);
        if (bad[si]) {
            out_colors[si] = {1.0f, 1.0f, 1.0f, 1.0f};
            continue;
        }
        const Scalar h   = heat[si];
        const Scalar v01 = (range > eps)
            ? (h - min_h) / (range + eps)
            : Scalar(0);  // all equal → map to viridis start
        out_colors[si] = map_scalar_to_color(v01);
    }
}

}  // namespace maya
}  // namespace rbfmax
