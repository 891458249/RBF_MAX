// =============================================================================
// rbfmax/solver.hpp
// -----------------------------------------------------------------------------
// Tikhonov-regularized RBF fit/predict pipeline.
//
// First non-header-only module: implementation lives in kernel/src/solver.cpp
// and is compiled into the static library `rbfmax_solver`.
//
// Module contract
// ---------------
//   * fit() returns a self-contained FitResult holding a copy of the centers
//     so the caller may release the input matrix immediately.
//   * Two fit() overloads:
//       - fit(centers, targets, options, lambda)   — fixed Tikhonov regulariser.
//       - fit(centers, targets, options, kLambdaAuto) — GCV auto-selection.
//   * Three-tier solver fallback (LLT → LDLT → BDCSVD), driven by Eigen's
//     info() flags. The chosen path is reported in FitResult::solver_path,
//     and condition_number is filled only on the BDCSVD branch.
//   * Polynomial tail (poly_degree ≥ 0) is solved via QR elimination — see
//     docs/spec/math_derivation.md §13.
//   * Lambda lower bound: kLambdaMin (1e-12). Smaller values are silently
//     clamped in Release; Debug builds trigger eigen_assert.
//   * All public functions are noexcept. Errors surface via the
//     FitResult::status enum; bad_alloc and other unexpected throws are
//     downgraded to FitStatus::INVALID_INPUT with FAILED solver_path.
//
// Typical usage
// -------------
//     using namespace rbfmax;
//     using namespace rbfmax::solver;
//
//     MatrixX centers(N, D), targets(N, M);
//     // ... populate ...
//     FitOptions opt(KernelParams{KernelType::kThinPlateSpline, 1.0}, 1);
//     FitResult fr = fit(centers, targets, opt, kLambdaAuto);
//     if (fr.status == FitStatus::OK) {
//         VectorX y_at_x = predict(fr, x_query);
//     }
// =============================================================================
#ifndef RBFMAX_SOLVER_HPP
#define RBFMAX_SOLVER_HPP

#include <cstdint>
#include <vector>

#include <Eigen/Core>

#include "rbfmax/feature_spec.hpp"
#include "rbfmax/kernel_functions.hpp"
#include "rbfmax/types.hpp"

namespace rbfmax {
namespace solver {

// -----------------------------------------------------------------------------
//  Tag dispatch for GCV-auto lambda
// -----------------------------------------------------------------------------

struct LambdaAuto {};
constexpr LambdaAuto kLambdaAuto{};

// -----------------------------------------------------------------------------
//  Lambda lower bound
// -----------------------------------------------------------------------------
//  Below this, Cholesky on the regularised matrix is empirically unreliable
//  on the RBF systems we target (see math_derivation.md §11.3).
// -----------------------------------------------------------------------------

constexpr Scalar kLambdaMin = static_cast<Scalar>(1e-12);

// -----------------------------------------------------------------------------
//  Configuration
// -----------------------------------------------------------------------------

/// Bundles a kernel choice with the polynomial-tail degree.  Three explicit
/// constructors (no default member initialisers) for strict C++11 aggregate
/// compliance — same fix as Slice 02.5.1 KernelParams.
struct FitOptions {
    KernelParams kernel;
    int poly_degree;  ///< -1 => no polynomial tail.

    FitOptions() noexcept
        : kernel(), poly_degree(minimum_polynomial_degree(kernel.type)) {}
    explicit FitOptions(const KernelParams& k) noexcept
        : kernel(k), poly_degree(minimum_polynomial_degree(k.type)) {}
    FitOptions(const KernelParams& k, int deg) noexcept
        : kernel(k), poly_degree(deg) {}
};

// -----------------------------------------------------------------------------
//  Solver-path & status enums
// -----------------------------------------------------------------------------

enum class SolverPath : std::int32_t {
    LLT    = 0,
    LDLT   = 1,
    BDCSVD = 2,
    FAILED = 3,
};

enum class FitStatus : std::int32_t {
    OK                    = 0,
    INSUFFICIENT_SAMPLES  = 1,
    SINGULAR_MATRIX       = 2,
    INVALID_INPUT         = 3,
};

// -----------------------------------------------------------------------------
//  Result bundle
// -----------------------------------------------------------------------------

/// Self-contained fit output.  Contains a copy of the training centers so the
/// caller may release the input matrix immediately after fit() returns.
struct FitResult {
    MatrixX      weights;           ///< N × M.
    MatrixX      poly_coeffs;       ///< Q × M; empty if poly_degree < 0.
    MatrixX      centers;           ///< N × D, owned copy.
    KernelParams kernel;
    int          poly_degree;
    Scalar       lambda_used;
    SolverPath   solver_path;
    FitStatus    status;
    Scalar       condition_number;  ///< -1 unless BDCSVD path was taken.
    Scalar       residual_norm;     ///< ||A w + P v - y||_F / ||y||_F.

    // -------------------------------------------------------------------------
    // Phase 2A.5 Slice 17A additions (ABI-additive tail fields).
    // All default-constructed to empty/zero when the legacy scalar-only
    // fit(centers, targets, opts, lambda) path is taken — this is the
    // 17A-SCALAR-ORACLE invariant, verified by the test group of the same
    // name in tests/test_feature_spec.cpp.
    //
    // Populated only by the heterogeneous fit() overloads when
    // spec.quat_blocks is non-empty (see feature_spec.hpp).
    //
    // sample_radii is intentionally NOT added here — it arrives in Slice 17F
    // with the proper name (no slice suffix), per Phase 2A.5 plan Decision 4.
    // -------------------------------------------------------------------------
    FeatureSpec          feature_spec;     ///< owned copy of fit's spec;
                                           ///< default (scalar_dim=0, no blocks)
                                           ///< in scalar-only fits.
    std::vector<MatrixX> quat_features;    ///< N × 4 per block (x,y,z,w);
                                           ///< empty for scalar-only fits.
    VectorX              feature_norms;    ///< cmt step-1 per-scalar-column L2;
                                           ///< empty for scalar-only fits.
    Scalar               distance_norm;    ///< cmt step-3 Frobenius of scalar
                                           ///< distance block; 0 for scalar-only.

    FitResult() noexcept
        : weights(),
          poly_coeffs(),
          centers(),
          kernel(),
          poly_degree(-1),
          lambda_used(0),
          solver_path(SolverPath::FAILED),
          status(FitStatus::INVALID_INPUT),
          condition_number(-1),
          residual_norm(0),
          feature_spec(),
          quat_features(),
          feature_norms(),
          distance_norm(0) {}
};

// -----------------------------------------------------------------------------
//  ScratchPool — pre-allocated buffers for the predict hot path
// -----------------------------------------------------------------------------

/// Scratch buffer pool for the predict hot path.
///
/// Pre-allocates all transient Eigen vectors used by predict so that
/// `predict_with_pool` executes zero heap allocations in its inner kernel
/// evaluation loop.  Designed for Maya 60fps compute() where per-frame
/// predict calls must not stress the allocator.
///
/// Thread-safety contract
/// ----------------------
/// ScratchPool is NOT thread-safe.  Each thread or TBB task must own its
/// own pool instance.  Concurrent predict_with_pool calls on the same pool
/// are a data race.
///
/// Lifetime contract
/// -----------------
/// The pool is independent of FitResult.  Size it to a FitResult's
/// centers/dim/poly_cols, reuse across predict calls for that FitResult.
/// Resize (construct a new pool) if you switch to a FitResult of different
/// shape.  Debug builds verify shape compatibility via eigen_assert.
class ScratchPool {
public:
    explicit ScratchPool(Index dim,
                         Index n_centers,
                         Index poly_cols = 0) noexcept;

    Index dim()       const noexcept { return query_vec.size(); }
    Index n_centers() const noexcept { return kernel_vals.size(); }
    Index poly_cols() const noexcept { return poly_vec.size(); }

    ScratchPool(const ScratchPool&)            = delete;
    ScratchPool& operator=(const ScratchPool&) = delete;
    ScratchPool(ScratchPool&&) noexcept            = default;
    ScratchPool& operator=(ScratchPool&&) noexcept = default;

    // Publicly exposed for zero-overhead access from predict_with_pool.
    VectorX query_vec;      // size = dim
    VectorX kernel_vals;    // size = n_centers
    VectorX diff_vec;       // size = dim
    VectorX poly_vec;       // size = poly_cols (may be 0)
};

// -----------------------------------------------------------------------------
//  Public API
// -----------------------------------------------------------------------------

/// Fit with a user-supplied Tikhonov lambda.  Lambda below kLambdaMin is
/// silently clamped (Debug build asserts).
FitResult fit(const Eigen::Ref<const MatrixX>& centers,
              const Eigen::Ref<const MatrixX>& targets,
              const FitOptions& options,
              Scalar lambda) noexcept;

/// Fit with GCV-selected lambda over a 50-point log-uniform grid in
/// [1e-10, 1e-2].  Falls back to lambda = 1e-6 if every grid point is
/// pathological.
FitResult fit(const Eigen::Ref<const MatrixX>& centers,
              const Eigen::Ref<const MatrixX>& targets,
              const FitOptions& options,
              LambdaAuto) noexcept;

// -----------------------------------------------------------------------------
//  Heterogeneous fit (Phase 2A.5 Slice 17A) — scalar + quaternion input blocks
// -----------------------------------------------------------------------------
//
//  Adds optional quaternion input blocks alongside the classical scalar
//  feature matrix.  Each row of `scalar_centers` corresponds to the same
//  row in every `quat_features[k]` and in `targets` — a single training
//  pose.
//
//  Layout of `quat_features`:
//    * size() must equal spec.quat_blocks.size()
//    * each MatrixX has shape N × 4 with column order (x, y, z, w)
//    * each row must be unit-length within kQuatIdentityEps (violation
//      surfaces as FitStatus::INVALID_INPUT)
//
//  Dispatch behavior (legacy compatibility — 17A-SCALAR-ORACLE invariant):
//    * If `spec.is_scalar_only()` (i.e. quat_blocks is empty), this
//      function explicitly calls the legacy
//      fit(scalar_centers, targets, options, lambda) overload and returns
//      its output verbatim.  The legacy function body is NOT refactored
//      into a shared core; byte-identical output with all pre-17A
//      diagnostic fields (status, solver_path, condition_number,
//      residual_norm) preserved.  The tail fields (feature_spec,
//      quat_features, feature_norms, distance_norm) remain
//      default-constructed in the scalar-only path.
//    * Otherwise, the cmt-style composite distance-matrix pipeline
//      (linearRegressionSolver.cpp setFeatures steps 1-6) builds the
//      heterogeneous matrix.  Slice 17A keeps rbfmax's classical
//      Tikhonov solver fallback (LLT → LDLT → BDCSVD); the one-hot θ
//      switch is deferred to Slice 17E.
//
FitResult fit(const Eigen::Ref<const MatrixX>& scalar_centers,
              const std::vector<MatrixX>& quat_features,
              const Eigen::Ref<const MatrixX>& targets,
              const FitOptions& options,
              const FeatureSpec& spec,
              Scalar lambda) noexcept;

FitResult fit(const Eigen::Ref<const MatrixX>& scalar_centers,
              const std::vector<MatrixX>& quat_features,
              const Eigen::Ref<const MatrixX>& targets,
              const FitOptions& options,
              const FeatureSpec& spec,
              LambdaAuto) noexcept;

/// Predict a single output channel (column 0 of weights/poly_coeffs).
Scalar predict_scalar(const FitResult& fr,
                      const Eigen::Ref<const VectorX>& x) noexcept;

/// Predict all output channels for one query point.  Returns an M-vector.
VectorX predict(const FitResult& fr,
                const Eigen::Ref<const VectorX>& x) noexcept;

/// Batched predict.  X is K × D; returns K × M.
MatrixX predict_batch(const FitResult& fr,
                      const Eigen::Ref<const MatrixX>& X) noexcept;

// -----------------------------------------------------------------------------
//  Zero-alloc predict overloads (pool-reuse)
// -----------------------------------------------------------------------------
//  Contract: pool shape must match the FitResult — pool.dim() ==
//  fr.centers.cols(), pool.n_centers() == fr.centers.rows(),
//  pool.poly_cols() == fr.poly_coeffs.rows().  Debug builds assert this via
//  eigen_assert; mismatched Release calls trigger an Eigen resize that
//  silently allocates (defeats the optimisation but stays correct).

/// Predict the first output column (M=1 specialisation) using a caller-owned
/// pool.  No heap allocation in the kernel evaluation loop.
Scalar predict_scalar_with_pool(const FitResult& fr,
                                const Eigen::Ref<const VectorX>& x,
                                ScratchPool& pool) noexcept;

/// Predict all M output channels using a caller-owned pool.  The returned
/// VectorX is a single allocation imposed by the by-value return type;
/// callers needing strict O(1) per-query must wait on Slice 07's
/// out-parameter API.
VectorX predict_with_pool(const FitResult& fr,
                          const Eigen::Ref<const VectorX>& x,
                          ScratchPool& pool) noexcept;

}  // namespace solver
}  // namespace rbfmax

#endif  // RBFMAX_SOLVER_HPP
