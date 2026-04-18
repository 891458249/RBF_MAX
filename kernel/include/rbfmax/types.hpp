// =============================================================================
// rbfmax/types.hpp
// -----------------------------------------------------------------------------
// Canonical scalar, vector and matrix type aliases for the rbfmax kernel.
//
// Design contract:
//   * Internal computation is ALWAYS performed in IEEE-754 64-bit double.
//   * Precision down-casting to 32-bit float happens only at external
//     boundaries (Maya MDataBlock, JSON I/O, UE5 export).
//   * Types are thin aliases over Eigen so that the kernel library remains
//     header-only, zero-ABI, and engine-agnostic.
//
// Supported compiler matrix:
//     MSVC 14.0 (2015)  ..  MSVC 17.3 (2022)
//     GCC  4.8.2         ..  GCC  11.2.1
//   C++11 only; no C++14 features permitted in this header.
// =============================================================================
#ifndef RBFMAX_TYPES_HPP
#define RBFMAX_TYPES_HPP

// Force Eigen into header-only mode and disable runtime assertions in
// release builds where we want the -O3 code-path to be free of branch
// misprediction hazards from Eigen's debug traps.
#if !defined(EIGEN_NO_DEBUG) && defined(NDEBUG)
#define EIGEN_NO_DEBUG 1
#endif

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <Eigen/Core>
#include <Eigen/Geometry>

namespace rbfmax {

// -----------------------------------------------------------------------------
//  Scalar & index primitives
// -----------------------------------------------------------------------------

/// All internal arithmetic happens in 64-bit IEEE-754.  This is the single
/// source of truth; any `float` appearing inside the kernel library is a bug.
using Scalar = double;

/// Signed index type — mirrors Eigen so that container sizes interoperate
/// with MatrixX::rows()/cols() without conversion warnings.
using Index = Eigen::Index;

// -----------------------------------------------------------------------------
//  Eigen shorthand aliases
// -----------------------------------------------------------------------------

template <int Rows, int Cols>
using MatrixNM = Eigen::Matrix<Scalar, Rows, Cols>;

template <int N>
using VectorN = Eigen::Matrix<Scalar, N, 1>;

using Vector2  = VectorN<2>;
using Vector3  = VectorN<3>;
using Vector4  = VectorN<4>;
using VectorX  = Eigen::Matrix<Scalar, Eigen::Dynamic, 1>;

using Matrix2  = MatrixNM<2, 2>;
using Matrix3  = MatrixNM<3, 3>;
using Matrix4  = MatrixNM<4, 4>;
using MatrixX  = Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>;

/// Row-major dynamic matrix — used when interfacing with external SOA layouts
/// (e.g. a contiguous sample block from Maya DataBlock or a JSON payload).
using RowMatrixX =
    Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

using Quaternion = Eigen::Quaternion<Scalar>;

// -----------------------------------------------------------------------------
//  Numerical constants
// -----------------------------------------------------------------------------
// `static constexpr` at namespace scope gives internal linkage under C++11,
// avoiding the ODR pitfalls of naked namespace-scope constexpr (which only
// becomes implicitly-inline from C++17 onward).
// -----------------------------------------------------------------------------

/// Numerical-zero threshold for generic scalar comparisons.
static constexpr Scalar kEps = static_cast<Scalar>(1e-12);

/// Cut-off below which log(r) is clamped to 0 (thin-plate spline limit).
static constexpr Scalar kLogEps = static_cast<Scalar>(1e-30);

/// Cut-off for a unit quaternion dot product before we treat two rotations
/// as identical.  Using 1 - kQuatIdentityEps on |dot(q0, q1)| dodges the
/// catastrophic cancellation in acos() near the antipode.
static constexpr Scalar kQuatIdentityEps = static_cast<Scalar>(1e-14);

static constexpr Scalar kPi      = static_cast<Scalar>(3.14159265358979323846);
static constexpr Scalar kTwoPi   = static_cast<Scalar>(6.28318530717958647692);
static constexpr Scalar kHalfPi  = static_cast<Scalar>(1.57079632679489661923);
static constexpr Scalar kInvPi   = static_cast<Scalar>(0.31830988618379067154);

// -----------------------------------------------------------------------------
//  Small numerical helpers (header-local, inlineable)
// -----------------------------------------------------------------------------

/// Returns `true` when `x` is finite (not NaN and not ±Inf).  Wrapper exists
/// because `std::isfinite` is a macro in some libstdc++-4.8 revisions which
/// does not survive aggressive inlining without the cmath include.
inline bool is_finite(Scalar x) noexcept {
    // Intentional use of != for NaN detection; `x == x` is UB-free and
    // produces the fastest code on both MSVC and GCC.
    return x == x
        && x != std::numeric_limits<Scalar>::infinity()
        && x != -std::numeric_limits<Scalar>::infinity();
}

/// Clamp `x` into the closed interval [lo, hi].  `std::clamp` is C++17 so
/// we roll our own.  NaN in `x` propagates (both comparisons are false).
inline Scalar clamp(Scalar x, Scalar lo, Scalar hi) noexcept {
    return (x < lo) ? lo : (hi < x ? hi : x);
}

/// Square a scalar without calling `std::pow`.  Compiles to a single mulsd
/// even at -O0 on both MSVC and GCC.
inline Scalar square(Scalar x) noexcept {
    return x * x;
}

}  // namespace rbfmax

#endif  // RBFMAX_TYPES_HPP
