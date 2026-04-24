// =============================================================================
// rbfmax/feature_spec.hpp
// -----------------------------------------------------------------------------
// Heterogeneous RBF input specification: scalar + per-block quaternion inputs
// with optional Swing/Twist decomposition per block.  Introduced in Phase 2A.5
// Slice 17A as the foundation for production pose-driven solving (Tekken 8
// AnimaDriver parity + chadvernon cmt rbfNode column convention).
//
// Design contract
// ---------------
//   * Header-only, zero ABI surface.  Types are plain POD-ish aggregates with
//     explicit C++11 constructors (no default member initialisers; same
//     Slice 02.5.1 convention as FitOptions / KernelParams).
//   * FeatureSpec is orthogonal to the legacy scalar-only fit() API.  When
//     `quat_blocks` is empty, the heterogeneous fit() overload dispatches to
//     the legacy path byte-identically (17A-SCALAR-ORACLE hard gate).
//   * Column layout (see Slice 17A §C audit derivation, DEVLOG 2026-04-24):
//       - Scalar block contributes N columns (one per training sample) when
//         scalar_dim > 0; zero otherwise.
//       - Each QuatBlock contributes cols_per_pose(space) * N columns:
//             Full                               → 1 column per pose
//             Swing | Twist | SwingTwist         → 2 columns per pose
//         Full mode is a 17A design choice — chadvernon cmt source does not
//         cover Full; cmt treats all quaternion inputs as Swing/Twist/
//         SwingTwist (2-col).  Slices 17B–17I preserve this convention.
//
// Usage
// -----
//     using namespace rbfmax;
//     FeatureSpec spec(/*scalar_dim=*/3);
//     spec.quat_blocks.push_back(QuatBlock(SolverSpace::SwingTwist,
//                                          Vector3::UnitY()));
//     Index total_cols = spec.total_distance_columns(N);
//     if (spec.is_scalar_only()) { /* legacy fast path */ }
// =============================================================================
#ifndef RBFMAX_FEATURE_SPEC_HPP
#define RBFMAX_FEATURE_SPEC_HPP

#include <cstdint>
#include <vector>

#include "rbfmax/types.hpp"

namespace rbfmax {

// -----------------------------------------------------------------------------
//  SolverSpace — per-quat-block rotation decomposition mode
// -----------------------------------------------------------------------------
//
//  Full       — no decomposition; one column per pose using
//               metric::quaternion_geodesic_distance directly.
//  Swing      — decompose q = swing * twist; use swing distance only (twist
//               column is emitted as 0 for layout stability).
//  Twist      — decompose q = swing * twist; use twist distance only.
//  SwingTwist — decompose q = swing * twist; emit both distances.
//
//  For Swing / Twist / SwingTwist, the QuatBlock::axis field specifies the
//  twist axis passed to rotation::decompose_swing_twist.  For Full, axis is
//  ignored at fit time but retained on the struct for schema round-trip.
//
enum class SolverSpace : std::int32_t {
    Full       = 0,
    Swing      = 1,
    Twist      = 2,
    SwingTwist = 3,
};

// -----------------------------------------------------------------------------
//  QuatBlock — single quaternion input block configuration
// -----------------------------------------------------------------------------

struct QuatBlock {
    SolverSpace space;
    Vector3     axis;  ///< Twist axis (unit length for Swing/Twist/SwingTwist;
                       ///< ignored for Full). Default-constructed to (0,0,0).

    QuatBlock() noexcept
        : space(SolverSpace::Full), axis(Vector3::Zero()) {}

    QuatBlock(SolverSpace s, const Vector3& ax) noexcept
        : space(s), axis(ax) {}
};

// -----------------------------------------------------------------------------
//  FeatureSpec — full heterogeneous input description
// -----------------------------------------------------------------------------
//
//  Invariant: scalar_dim >= 0 and quat_blocks may be empty.  The scalar-only
//  predicate `is_scalar_only()` returns true iff quat_blocks is empty, and
//  this is the condition that triggers legacy fit() delegation in Slice 17A.
//
//  `scalar_dim == 0 && quat_blocks.empty()` is a degenerate spec; validation
//  in solver::fit rejects it via FitStatus::INVALID_INPUT.
//
struct FeatureSpec {
    Index                  scalar_dim;    ///< number of scalar input axes
    std::vector<QuatBlock> quat_blocks;   ///< empty => scalar-only

    FeatureSpec() noexcept
        : scalar_dim(0), quat_blocks() {}

    explicit FeatureSpec(Index d) noexcept
        : scalar_dim(d), quat_blocks() {}

    FeatureSpec(Index d, const std::vector<QuatBlock>& blocks)
        : scalar_dim(d), quat_blocks(blocks) {}

    /// True iff no quat_blocks are configured.  The heterogeneous solver
    /// dispatches to the legacy scalar-only fit() code path when this is
    /// true, guaranteeing byte-identical output via 17A-SCALAR-ORACLE.
    bool is_scalar_only() const noexcept {
        return quat_blocks.empty();
    }

    /// Columns contributed by a single QuatBlock per training pose under
    /// a given SolverSpace.  Full → 1; Swing | Twist | SwingTwist → 2.
    /// Encodes the Drift #3 fix from the 17A pre-dispatch audit.
    static Index cols_per_pose(SolverSpace s) noexcept {
        return (s == SolverSpace::Full) ? Index(1) : Index(2);
    }

    /// Total number of distance-matrix columns assembled at fit time for
    /// N training samples under this spec.
    ///
    ///   total = (scalar_dim > 0 ? N : 0)
    ///         + Σ_k cols_per_pose(quat_blocks[k].space) * N
    Index total_distance_columns(Index N) const noexcept {
        Index cols = (scalar_dim > 0) ? N : Index(0);
        for (std::size_t i = 0; i < quat_blocks.size(); ++i) {
            cols += cols_per_pose(quat_blocks[i].space) * N;
        }
        return cols;
    }
};

}  // namespace rbfmax

#endif  // RBFMAX_FEATURE_SPEC_HPP
