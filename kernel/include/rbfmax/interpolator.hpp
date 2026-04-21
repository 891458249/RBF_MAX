// =============================================================================
// rbfmax/interpolator.hpp
// -----------------------------------------------------------------------------
// Phase 1 integration facade: a single user-facing class composing kernel,
// distance, kdtree, solver, and ScratchPool into a unified fit→predict
// lifecycle.  Users no longer manage FitResult or ScratchPool directly.
//
// Core contracts
// --------------
//   1. NOT thread-safe on a single instance.  RBFInterpolator owns a
//      mutable ScratchPool and KNN scratch buffers; concurrent predict()
//      calls on the same instance race.  For multi-thread predict, call
//      `clone()` per worker thread/TBB task.
//   2. fit() completely replaces prior state.  No history is retained;
//      the second fit() is equivalent to constructing a new
//      RBFInterpolator with the same options.
//   3. kd-tree KNN acceleration only activates for KernelType::kGaussian
//      (and only when n_centers >= options.kdtree_threshold and
//      !options.force_dense).  Other kernels traverse all centers — see
//      docs/math_derivation.md §14 for the truncation-error rationale.
//
// Typical usage
// -------------
//     using namespace rbfmax;
//     InterpolatorOptions opts(KernelParams{KernelType::kGaussian, 1.0});
//     RBFInterpolator rbf(opts);
//     rbf.fit(centers, targets, solver::kLambdaAuto);
//     for (...) {
//         VectorX y = rbf.predict(x_query);
//     }
// =============================================================================
#ifndef RBFMAX_INTERPOLATOR_HPP
#define RBFMAX_INTERPOLATOR_HPP

#include <memory>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "rbfmax/kdtree.hpp"
#include "rbfmax/kernel_functions.hpp"
#include "rbfmax/solver.hpp"
#include "rbfmax/types.hpp"

namespace rbfmax {

/// Bundled configuration for RBFInterpolator.  All fields have safe
/// defaults; the most common construction is from a single KernelParams.
struct InterpolatorOptions {
    KernelParams kernel;
    int          poly_degree;        ///< -1 = auto (use minimum_polynomial_degree)
    Index        kdtree_threshold;   ///< n_centers >= this => kdtree (Gaussian only)
    Index        knn_neighbors;      ///< 0 = auto (min(N, 32) for Gaussian)
    bool         force_dense;        ///< debug override; if true, never use kdtree

    InterpolatorOptions() noexcept;
    explicit InterpolatorOptions(const KernelParams& k) noexcept;
};

/// Facade combining kernel + distance + kdtree + solver + ScratchPool.
class RBFInterpolator {
public:
    explicit RBFInterpolator(
        const InterpolatorOptions& opts = InterpolatorOptions{}) noexcept;

    // Move-only: ScratchPool is non-copyable, KdTree references the owned
    // FitResult::centers buffer (deep copy must be explicit via clone()).
    RBFInterpolator(const RBFInterpolator&)            = delete;
    RBFInterpolator& operator=(const RBFInterpolator&) = delete;
    RBFInterpolator(RBFInterpolator&& other) noexcept;
    RBFInterpolator& operator=(RBFInterpolator&& other) noexcept;
    ~RBFInterpolator() = default;

    /// Deep copy for per-thread predict.  Rebuilds the kd-tree against
    /// the copy's own centers buffer so the two instances are fully
    /// independent.
    RBFInterpolator clone() const;

    // ---- Training -------------------------------------------------------
    // fit() replaces any prior state completely (decision 2).

    solver::FitStatus fit(const Eigen::Ref<const MatrixX>& centers,
                          const Eigen::Ref<const MatrixX>& targets,
                          Scalar lambda) noexcept;

    solver::FitStatus fit(const Eigen::Ref<const MatrixX>& centers,
                          const Eigen::Ref<const MatrixX>& targets,
                          solver::LambdaAuto = solver::kLambdaAuto) noexcept;

    // ---- Prediction -----------------------------------------------------
    // NOT thread-safe on a single instance; clone() per thread.

    Scalar  predict_scalar(const Eigen::Ref<const VectorX>& x) const noexcept;
    VectorX predict       (const Eigen::Ref<const VectorX>& x) const noexcept;
    MatrixX predict_batch (const Eigen::Ref<const MatrixX>& X) const noexcept;

    // ---- State queries --------------------------------------------------

    bool                is_fitted()        const noexcept;
    solver::FitStatus   status()           const noexcept;
    solver::SolverPath  solver_path()      const noexcept;
    Index               n_centers()        const noexcept;
    Index               dim()              const noexcept;
    Scalar              lambda_used()      const noexcept;
    Scalar              condition_number() const noexcept;
    bool                uses_kdtree()      const noexcept;

    /// The kernel parameters currently in effect.  After fit() or load()
    /// this reflects the kernel stored in the FitResult (the one actually
    /// used for training), not the one passed at construction.  Undefined
    /// semantics before the first successful fit/load — callers should
    /// gate this on is_fitted().
    ///
    /// Added in Phase 2A Slice 11 to let consumers (notably the Maya
    /// node) report kernel type without re-parsing the saved JSON.
    const KernelParams& kernel_params()   const noexcept;

    /// The centers matrix (N × D) currently in effect.  After fit() or
    /// load() this reflects the centers stored in the FitResult (the
    /// owned copy Phase 1's solver/io_json populate).  Before the first
    /// successful fit/load, returns a default-constructed 0×0 matrix.
    /// Gate on is_fitted() for defined semantics.
    ///
    /// Added in Phase 2B Slice 13 to let the Viewport 2.0 DrawOverride
    /// render centers without re-parsing the saved JSON or re-creating
    /// the interpolator internals.
    const MatrixX&      centers()         const noexcept;

    // ---- Persistence (Slice 08) -----------------------------------------
    // Convenience methods that delegate to rbfmax::io_json::save / load.
    // Returns true on success; false on any failure (file I/O, schema
    // mismatch, parse error).  On load failure, this interpolator's
    // state is unchanged (atomic update semantics).
    bool save(const std::string& path) const noexcept;
    bool load(const std::string& path)       noexcept;

private:
    InterpolatorOptions               opts_;
    solver::FitResult                 fit_result_;
    bool                              fitted_;
    std::unique_ptr<spatial::KdTree>  kdtree_;        // lazy; nullptr unless KNN path engaged

    // mutable: predict is logically const, but the pool / KNN buffers are
    // per-call scratch storage that we need to mutate in const methods.
    mutable solver::ScratchPool       pool_;
    mutable std::vector<Index>        indices_buf_;   // KNN result indices
    mutable std::vector<Scalar>       sq_dist_buf_;   // KNN result squared distances

    bool  should_use_kdtree()      const noexcept;
    Index effective_k_neighbors()  const noexcept;

    /// Predict via KNN truncation (Gaussian-only path).
    VectorX predict_knn(const Eigen::Ref<const VectorX>& x) const noexcept;
};

}  // namespace rbfmax

#endif  // RBFMAX_INTERPOLATOR_HPP
