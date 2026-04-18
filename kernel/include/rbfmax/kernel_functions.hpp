// =============================================================================
// rbfmax/kernel_functions.hpp
// -----------------------------------------------------------------------------
// Radial basis kernel catalogue.
//
// Contract
// --------
//   * All kernels map a non-negative radial distance r ∈ [0, +∞) to a scalar.
//     Passing a negative r is a caller-side contract violation (r stems from
//     a norm and must not go negative).
//   * Observed behaviour for negative r (stable across releases but not a
//     guaranteed part of the API):
//         Linear / Cubic / Quintic   -> natural odd extension  (φ(-r) = -φ(r)).
//         Gaussian / IMQ             -> natural even extension (φ(-r) =  φ(r)).
//         ThinPlateSpline            -> clamped to 0 via the r ≤ kLogEps branch.
//     No kernel segfaults for negative r; NaN still propagates.
//   * NaN input always produces NaN output (fail-fast, no silent masking).
//   * Kernels that require a shape parameter (Gaussian, IMQ, MQ) take it
//     as an explicit argument; non-shape kernels ignore it.
//   * Each kernel φ has a companion φ' (dphi/dr) whose limit at r=0 is
//     analytically resolved — no NaN leakage through finite-difference
//     pipelines that consume the derivative.
//
// Kernel catalogue  (symbols match the docx architecture document)
// ----------------------------------------------------------------
//     Tag                        φ(r)                        Min-poly-deg
//     ------------------------   -------------------------   ------------
//     Linear                     r                           0
//     Cubic                      r^3                         1
//     Quintic                    r^5                         2
//     ThinPlateSpline            r^2 * ln(r)   (φ(0)=0)      1
//     Gaussian                   exp(-(ε r)^2)               — (pos-def)
//     InverseMultiquadric        1 / sqrt(1 + (ε r)^2)       — (pos-def)
//
// The minimum polynomial degree is the smallest k such that the augmented
// RBF system
//     Σ_j w_j φ(||x - c_j||)  +  Σ_α v_α P_α(x)
// is conditionally positive definite of order ≤ k.  This number is consumed
// by the solver when it sizes the polynomial tail.
// =============================================================================
#ifndef RBFMAX_KERNEL_FUNCTIONS_HPP
#define RBFMAX_KERNEL_FUNCTIONS_HPP

#include <cmath>
#include <cstring>
#include <limits>

#include "rbfmax/types.hpp"

namespace rbfmax {

// -----------------------------------------------------------------------------
//  Kernel taxonomy
// -----------------------------------------------------------------------------

enum class KernelType : std::uint8_t {
    kLinear              = 0,
    kCubic               = 1,
    kQuintic             = 2,
    kThinPlateSpline     = 3,
    kGaussian            = 4,
    kInverseMultiquadric = 5,
};

/// Bundles a kernel tag with its shape parameter.  Non-shape kernels leave
/// `eps` unused; we still store it so that callers may switch kernels at
/// runtime without re-plumbing parameter vectors.
struct KernelParams {
    KernelType type {KernelType::kGaussian};
    Scalar     eps  {static_cast<Scalar>(1.0)};  // Only used by Gaussian/IMQ.
};

/// Returns the minimum polynomial degree required to guarantee
/// conditional positive definiteness of the interpolation matrix
/// augmented with a polynomial tail.  A return value of -1 means the
/// kernel is strictly positive definite and no polynomial tail is needed.
inline int minimum_polynomial_degree(KernelType k) noexcept {
    switch (k) {
        case KernelType::kLinear:              return 0;
        case KernelType::kCubic:               return 1;
        case KernelType::kQuintic:             return 2;
        case KernelType::kThinPlateSpline:     return 1;
        case KernelType::kGaussian:            return -1;
        case KernelType::kInverseMultiquadric: return -1;
    }
    // Unreachable under a well-formed KernelType; both MSVC and GCC can
    // prove this, so no runtime cost.  Fall-through guard satisfies -Wreturn.
    return -1;
}

/// `true` iff the kernel's positive-definiteness relies on a shape parameter.
inline bool requires_shape_parameter(KernelType k) noexcept {
    return k == KernelType::kGaussian
        || k == KernelType::kInverseMultiquadric;
}

// -----------------------------------------------------------------------------
//  Individual kernel evaluators  (free inline functions)
// -----------------------------------------------------------------------------
//  Each kernel is expressed as a short, force-inlineable function so that
//  downstream template specialisations can bake the call site away.
// -----------------------------------------------------------------------------

// --- Linear:  φ(r) = r,     φ'(r) = 1 ----------------------------------------
inline Scalar linear(Scalar r) noexcept {
    return r;
}
inline Scalar linear_derivative(Scalar /*r*/) noexcept {
    return static_cast<Scalar>(1.0);
}

// --- Cubic:   φ(r) = r^3,   φ'(r) = 3 r^2 ------------------------------------
inline Scalar cubic(Scalar r) noexcept {
    return r * r * r;
}
inline Scalar cubic_derivative(Scalar r) noexcept {
    return static_cast<Scalar>(3.0) * r * r;
}

// --- Quintic: φ(r) = r^5,   φ'(r) = 5 r^4 ------------------------------------
inline Scalar quintic(Scalar r) noexcept {
    const Scalar r2 = r * r;
    return r2 * r2 * r;
}
inline Scalar quintic_derivative(Scalar r) noexcept {
    const Scalar r2 = r * r;
    return static_cast<Scalar>(5.0) * r2 * r2;
}

// --- Thin-plate spline:  φ(r) = r^2 ln(r),  with φ(0) := 0 -------------------
//
// Limit derivation (see docs/math_derivation.md):
//     lim_{r→0+} r^2 ln(r) = 0           (since r^2 dominates ln(r)).
//     φ'(r) = r (2 ln(r) + 1);  lim_{r→0+} φ'(r) = 0.
//
// Clamping at r < kLogEps protects against log(0) = -inf propagating a NaN
// through the otherwise well-defined r^2·ln(r) product.
inline Scalar thin_plate_spline(Scalar r) noexcept {
    if (r <= kLogEps) {
        // NaN input must still propagate — kLogEps comparison returns false
        // for NaN, so this branch safely lets NaN through to the fallback.
        if (r != r) return r;  // NaN passthrough.
        return static_cast<Scalar>(0.0);
    }
    return r * r * std::log(r);
}
inline Scalar thin_plate_spline_derivative(Scalar r) noexcept {
    if (r <= kLogEps) {
        if (r != r) return r;
        return static_cast<Scalar>(0.0);
    }
    return r * (static_cast<Scalar>(2.0) * std::log(r) + static_cast<Scalar>(1.0));
}

// --- Gaussian:  φ(r) = exp(-(ε r)^2),  φ'(r) = -2 ε^2 r · φ(r) ---------------
inline Scalar gaussian(Scalar r, Scalar eps) noexcept {
    const Scalar er = eps * r;
    return std::exp(-(er * er));
}
inline Scalar gaussian_derivative(Scalar r, Scalar eps) noexcept {
    const Scalar er = eps * r;
    return static_cast<Scalar>(-2.0) * eps * eps * r * std::exp(-(er * er));
}

// --- Inverse multiquadric: φ(r) = 1/sqrt(1 + (ε r)^2) ------------------------
// φ'(r) = -ε^2 r · (1 + (ε r)^2)^(-3/2)
inline Scalar inverse_multiquadric(Scalar r, Scalar eps) noexcept {
    const Scalar er = eps * r;
    return static_cast<Scalar>(1.0) / std::sqrt(static_cast<Scalar>(1.0) + er * er);
}
inline Scalar inverse_multiquadric_derivative(Scalar r, Scalar eps) noexcept {
    const Scalar er  = eps * r;
    const Scalar den = static_cast<Scalar>(1.0) + er * er;
    // Single std::sqrt + division avoids the precision loss of two std::pow
    // calls with fractional exponents.
    return -eps * eps * r / (den * std::sqrt(den));
}

// -----------------------------------------------------------------------------
//  Runtime dispatchers
// -----------------------------------------------------------------------------
//  Switch-based dispatch is preferred over virtual dispatch here because:
//    (1) The kernel type is set once per RBF node and doesn't change
//        per-sample, so modern branch predictors nail the target after a
//        single warm-up iteration.
//    (2) Enabling LTO allows the optimiser to fold these switches away at
//        call sites that know the kernel statically.
// -----------------------------------------------------------------------------

inline Scalar evaluate_kernel(KernelType k, Scalar r, Scalar eps) noexcept {
    switch (k) {
        case KernelType::kLinear:              return linear(r);
        case KernelType::kCubic:               return cubic(r);
        case KernelType::kQuintic:             return quintic(r);
        case KernelType::kThinPlateSpline:     return thin_plate_spline(r);
        case KernelType::kGaussian:            return gaussian(r, eps);
        case KernelType::kInverseMultiquadric: return inverse_multiquadric(r, eps);
    }
    return std::numeric_limits<Scalar>::quiet_NaN();
}

inline Scalar evaluate_kernel(const KernelParams& p, Scalar r) noexcept {
    return evaluate_kernel(p.type, r, p.eps);
}

inline Scalar evaluate_kernel_derivative(KernelType k, Scalar r, Scalar eps) noexcept {
    switch (k) {
        case KernelType::kLinear:              return linear_derivative(r);
        case KernelType::kCubic:               return cubic_derivative(r);
        case KernelType::kQuintic:             return quintic_derivative(r);
        case KernelType::kThinPlateSpline:     return thin_plate_spline_derivative(r);
        case KernelType::kGaussian:            return gaussian_derivative(r, eps);
        case KernelType::kInverseMultiquadric: return inverse_multiquadric_derivative(r, eps);
    }
    return std::numeric_limits<Scalar>::quiet_NaN();
}

inline Scalar evaluate_kernel_derivative(const KernelParams& p, Scalar r) noexcept {
    return evaluate_kernel_derivative(p.type, r, p.eps);
}

// -----------------------------------------------------------------------------
//  Canonical string tags  (used by io_json.hpp in a later slice)
// -----------------------------------------------------------------------------
//  We expose them here so that round-trip I/O never depends on the order
//  of the enum — renaming a tag in serialized assets is a breaking change
//  that must be coordinated with the JSON schema version header.
// -----------------------------------------------------------------------------

inline const char* kernel_type_to_string(KernelType k) noexcept {
    switch (k) {
        case KernelType::kLinear:              return "Linear";
        case KernelType::kCubic:               return "Cubic";
        case KernelType::kQuintic:             return "Quintic";
        case KernelType::kThinPlateSpline:     return "ThinPlateSpline";
        case KernelType::kGaussian:            return "Gaussian";
        case KernelType::kInverseMultiquadric: return "InverseMultiquadric";
    }
    return "Unknown";
}

/// Returns `true` and writes the result into `out` iff `tag` matches a
/// known kernel name exactly (case-sensitive).  Never throws.
inline bool kernel_type_from_string(const char* tag, KernelType& out) noexcept {
    if (tag == nullptr) {
        return false;
    }
    // Cheap char-switch on first character to avoid strcmp for most calls.
    switch (tag[0]) {
        case 'L':
            if (std::strcmp(tag, "Linear") == 0) {
                out = KernelType::kLinear;
                return true;
            }
            break;
        case 'C':
            if (std::strcmp(tag, "Cubic") == 0) {
                out = KernelType::kCubic;
                return true;
            }
            break;
        case 'Q':
            if (std::strcmp(tag, "Quintic") == 0) {
                out = KernelType::kQuintic;
                return true;
            }
            break;
        case 'T':
            if (std::strcmp(tag, "ThinPlateSpline") == 0) {
                out = KernelType::kThinPlateSpline;
                return true;
            }
            break;
        case 'G':
            if (std::strcmp(tag, "Gaussian") == 0) {
                out = KernelType::kGaussian;
                return true;
            }
            break;
        case 'I':
            if (std::strcmp(tag, "InverseMultiquadric") == 0) {
                out = KernelType::kInverseMultiquadric;
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

}  // namespace rbfmax

#endif  // RBFMAX_KERNEL_FUNCTIONS_HPP
