// =============================================================================
// rbfmax/quaternion.hpp
// -----------------------------------------------------------------------------
// Quaternion algebra primitives for pose-space RBF interpolation.
//
// API summary
// -----------
//   * decompose_swing_twist(q, axis) -> SwingTwist{swing, twist}
//       Projection-based decomposition q = swing * twist along a chosen axis.
//   * log_map(q)                     -> Vector3
//       SO(3) -> so(3) ≃ ℝ³ rotation-vector logarithm with shortest-path
//       double-cover handling.
//   * exp_map(v)                     -> Quaternion
//       so(3) ≃ ℝ³ -> SO(3) exponential, inverse of log_map on |v| < π.
//
// Contract checklist (consumed by upstream solver and Maya node):
//   1. Unit-axis contract — `axis` to decompose_swing_twist must satisfy
//      |axis|² = 1. Enforced via eigen_assert in Debug; trusted in Release.
//   2. Double-cover convention — log_map flips q internally to its
//      shortest-path representative (w >= 0). exp_map always returns a
//      quaternion with w >= 0 because cos(|v|/2) >= 0 for |v|/2 ∈ [0, π/2].
//   3. Taylor threshold — both log_map and exp_map switch to a Taylor
//      branch when the magnitude of the small-angle term drops below
//      1e-8. See docs/spec/math_derivation.md §8.4 for the error analysis
//      (truncation O(1e-32) ≪ ε_mach).
//
// All routines are header-only, noexcept, and never allocate.
// =============================================================================
#ifndef RBFMAX_QUATERNION_HPP
#define RBFMAX_QUATERNION_HPP

#include <cmath>

#include "rbfmax/types.hpp"

namespace rbfmax {
namespace rotation {

/// Result of a swing-twist decomposition: q ≡ swing * twist.
///
///   * `swing` rotates the chosen axis to its image under q (no twist).
///   * `twist` is a pure rotation about the chosen axis.
struct SwingTwist {
    Quaternion swing;
    Quaternion twist;
};

/// Decomposes a unit quaternion `q` into swing + twist around `axis`.
///
/// Contract: `axis` is unit-length (Debug-asserted via eigen_assert).
///
/// Singularity convention: when q rotates `axis` to `-axis` (180°
/// "swing" perpendicular to axis, i.e. q.w == 0 and q.vec ⊥ axis),
/// the twist is unobservable; this routine returns
///     twist = Identity, swing = q
/// so that `swing * twist == q` still holds. See math_derivation.md §7.3.
inline SwingTwist decompose_swing_twist(const Quaternion& q,
                                        const Vector3& axis) noexcept {
    eigen_assert(std::fabs(axis.squaredNorm() - Scalar(1)) < Scalar(1e-6));

    // Project the imaginary part of q onto `axis`.  The "twist proxy"
    // (q.w, p) — once normalized — is a pure rotation about `axis`.
    const Vector3 v = q.vec();
    const Vector3 p = axis * v.dot(axis);
    const Scalar denom_sq = q.w() * q.w() + p.squaredNorm();

    SwingTwist out;
    if (denom_sq < kEps * kEps) {
        // Singular branch: q sends axis -> -axis; twist undefined.
        // See math_derivation.md §7.3.
        out.twist = Quaternion::Identity();
        out.swing = q;
    } else {
        out.twist = Quaternion(q.w(), p.x(), p.y(), p.z()).normalized();
        out.swing = q * out.twist.conjugate();
    }
    return out;
}

/// Quaternion logarithm map onto the rotation-vector representation in ℝ³.
///
/// For a unit quaternion q = (cos(θ/2), sin(θ/2)·n) the logarithm is the
/// rotation vector r = θ·n where θ = 2·asin(|q.vec|).
///
/// Double-cover convention: q and -q describe the same rotation, but the
/// logarithm differs by 2π in magnitude. We pick the shortest-path
/// representative by flipping q when q.w < 0 — standard convention for
/// any interpolation pipeline. (At q.w == 0 the choice is intrinsically
/// ambiguous; tests filter that boundary or compare with sign-folded
/// distance.)
///
/// Taylor branch: when |q.vec| < 1e-8 we switch to the second-order
/// expansion 2·v·(1 + |v|²/6) which avoids dividing by a vanishing |v|
/// while preserving full double precision (see math_derivation.md §8.4).
inline Vector3 log_map(const Quaternion& q) noexcept {
    // Shortest-path: choose representative with w >= 0.
    const Quaternion q_eff = (q.w() < Scalar(0))
        ? Quaternion(-q.w(), -q.x(), -q.y(), -q.z())
        : q;

    const Vector3 v = q_eff.vec();
    const Scalar v_norm = v.norm();

    if (v_norm < Scalar(1e-8)) {
        // Near identity: 2·asin(x)/x = 2 + x²/3 + O(x⁴); factor `v` in
        // and rewrite as 2·v·(1 + |v|²/6).  See math_derivation.md §8.4.
        return Scalar(2) * v * (Scalar(1) + v_norm * v_norm / Scalar(6));
    }

    // Standard form.  The argument to asin is a norm of a sub-vector of a
    // unit quaternion, so it lives in [0, 1] up to ULP drift.  A
    // single-sided clamp guards against asin(1+ε) returning NaN when an
    // upstream non-unit input slips past the contract.
    const Scalar arg = (v_norm < Scalar(1)) ? v_norm : Scalar(1);
    return v * (Scalar(2) * std::asin(arg) / v_norm);
}

/// Quaternion exponential map from a rotation vector r ∈ ℝ³ to a unit
/// quaternion (cos(|r|/2), sin(|r|/2)·r/|r|).
///
/// Output always has w ∈ [0, 1] for |r| ≤ π and is unit-norm to within
/// ε_mach. exp_map(0) = Identity exactly.
///
/// Taylor branch: when |r| < 1e-8 the routine evaluates
///     w   ≈ 1 - θ²/8
///     xyz ≈ (1/2 - θ²/48)·r
/// instead of sin/cos, preserving precision near the identity (truncation
/// O(θ⁴) ≈ 1e-32 ≪ ε_mach).
inline Quaternion exp_map(const Vector3& v) noexcept {
    const Scalar theta = v.norm();
    const Scalar half_theta = theta / Scalar(2);

    if (theta < Scalar(1e-8)) {
        // Taylor near θ = 0; see math_derivation.md §8.4.
        const Scalar sinc_half = Scalar(0.5) - theta * theta / Scalar(48);
        const Scalar w_part = Scalar(1) - theta * theta / Scalar(8);
        return Quaternion(w_part,
                          sinc_half * v.x(),
                          sinc_half * v.y(),
                          sinc_half * v.z());
    }

    // Standard form.  Note: divisor is `theta`, not `half_theta`, so that
    // sinc_half * v.xyz reconstructs sin(θ/2)·n correctly.
    const Scalar sinc_half = std::sin(half_theta) / theta;
    return Quaternion(std::cos(half_theta),
                      sinc_half * v.x(),
                      sinc_half * v.y(),
                      sinc_half * v.z());
}

}  // namespace rotation
}  // namespace rbfmax

#endif  // RBFMAX_QUATERNION_HPP
