// =============================================================================
// rbfmax/distance.hpp
// -----------------------------------------------------------------------------
// Distance metrics for the RBF kernel.
//
// Exposes two families:
//
//   1. Euclidean metrics on arbitrary Eigen vectors (Vector3, VectorX, Map<>…).
//      The template signature takes any MatrixBase-derived expression so the
//      hot path never forces a temporary heap-allocated VectorX.
//
//        Scalar squared_distance(a, b);   // kdtree-friendly, no sqrt
//        Scalar distance        (a, b);   // general purpose
//
//   2. Quaternion geodesic metric on SO(3), with antipodal reduction and a
//      numerically-stable half-angle branch near d=1.
//
//        Scalar quaternion_geodesic_distance(q1, q2);
//        Scalar quaternion_abs_dot         (q1, q2);   // raw |q1·q2|
//
// Contract for the quaternion metric
// ----------------------------------
//   * Inputs MUST be unit quaternions (‖q‖ = 1). Enforcement is done via
//     `assert()` in debug builds only; release builds trust the caller so
//     that the per-sample hot path stays branch-free. The upstream Maya
//     node is responsible for one normalisation at attribute ingress.
//   * The zero quaternion (0,0,0,0) is undefined and will hit the debug
//     assert. In release builds it silently produces |dot|=0, i.e. π/2,
//     which is not a meaningful rotation distance — callers must screen
//     for zero-norm inputs upstream.
//   * NaN components propagate (the acos/asin chain preserves NaN).
//
// Error bounds (see docs/math_derivation.md §6)
// ---------------------------------------------
//   * Regular acos branch       : |Δd| ≲ √ε_mach ≈ 1.5e-8 (relative).
//   * asin half-angle fallback  : |Δd| ≲ 1e-7 near the identity — this is
//     a physical precision floor, not a bug.
// =============================================================================
#ifndef RBFMAX_DISTANCE_HPP
#define RBFMAX_DISTANCE_HPP

#include <algorithm>
#include <cassert>
#include <cmath>

#include "rbfmax/types.hpp"

namespace rbfmax {
namespace metric {

// =============================================================================
//  Euclidean
// =============================================================================

/// Squared Euclidean distance  ‖a − b‖²  — cheapest form, used by KD-Tree
/// nearest-neighbour queries where a monotonic surrogate of the true
/// distance is sufficient.
///
/// The template accepts any Eigen expression compatible with the
/// `operator-` / `squaredNorm()` chain (Vector3, VectorX, Map<>, blocks).
/// Mismatched compile-time sizes are caught by Eigen's static assertion.
template <typename DerivedA, typename DerivedB>
inline Scalar squared_distance(const Eigen::MatrixBase<DerivedA>& a,
                               const Eigen::MatrixBase<DerivedB>& b) noexcept {
    return (a.derived() - b.derived()).squaredNorm();
}

/// Euclidean distance  ‖a − b‖  — general-purpose metric.
template <typename DerivedA, typename DerivedB>
inline Scalar distance(const Eigen::MatrixBase<DerivedA>& a,
                       const Eigen::MatrixBase<DerivedB>& b) noexcept {
    return (a.derived() - b.derived()).norm();
}

// =============================================================================
//  Quaternion geodesic
// =============================================================================

/// Absolute inner product  |q1·q2|  with antipodal reduction already applied.
///
/// Useful when a downstream solver needs the raw dot but wants to skip the
/// trigonometric conversion (e.g. a Gaussian kernel that operates directly
/// on cosine-similarity space).  Expects unit quaternions.
inline Scalar quaternion_abs_dot(const Quaternion& q1,
                                 const Quaternion& q2) noexcept {
    return std::fabs(q1.dot(q2));
}

/// Geodesic distance on the unit 3-sphere, reduced to the quotient
/// S³ / {±1} ≃ SO(3).
///
///     d(q1, q2) = 2·acos( |q1·q2| )
///
/// Numerics
/// --------
/// Two branches guard against the two places where floating-point kills
/// acos:
///
///   (a) |dot| can exceed 1 by a few ULPs after the multiply-add chain
///       even for perfectly unit inputs.  We clamp the argument of acos
///       to [-1, 1] (only the upper bound matters because we took |·|).
///
///   (b) Near the identity (|dot| → 1⁻) acos suffers catastrophic
///       cancellation: 1 − dot loses 7-8 significant digits.  We switch
///       to the equivalent half-angle form
///           d = 2·asin( √(1 − dot²) )
///       which samples the argument in a well-conditioned region.
inline Scalar quaternion_geodesic_distance(const Quaternion& q1,
                                           const Quaternion& q2) noexcept {
    // Debug-only unit-norm contract.  Release builds skip this so the hot
    // path remains two multiplies + one acos/asin.
    assert(std::fabs(q1.squaredNorm() - Scalar(1)) < Scalar(1e-6));
    assert(std::fabs(q2.squaredNorm() - Scalar(1)) < Scalar(1e-6));

    const Scalar d = std::fabs(q1.dot(q2));  // antipodal reduction.

    if (d >= Scalar(1) - kQuatIdentityEps) {
        // Near-identity: half-angle asin is stable, acos is not.
        // max(0, …) guards against a negative argument from ULP drift.
        const Scalar s = std::sqrt(std::max<Scalar>(Scalar(0), Scalar(1) - d * d));
        return Scalar(2) * std::asin(s);
    }
    // Regular region: clamp defends acos's domain against d > 1 ULP drift.
    return Scalar(2) * std::acos(clamp(d, Scalar(-1), Scalar(1)));
}

}  // namespace metric
}  // namespace rbfmax

#endif  // RBFMAX_DISTANCE_HPP
