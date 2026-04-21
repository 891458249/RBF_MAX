// =============================================================================
// kernel/src/interpolator.cpp
// -----------------------------------------------------------------------------
// Implementation of rbfmax::RBFInterpolator — see interpolator.hpp for the
// public contract.
//
// Composition layout
// ------------------
//   * fit() forwards to rbfmax::solver::fit(), then optionally builds a
//     spatial::KdTree (Gaussian + N >= threshold + !force_dense).
//   * predict() routes to either solver::predict_with_pool() (dense) or
//     this file's predict_knn() (KNN-truncated).
//   * clone() rebuilds the kd-tree against the copy's own centers buffer
//     so the two instances do not share storage.
//
// build_polynomial_row_local() duplicates solver.cpp's anonymous
// namespace helper.  Accepted as tech debt rather than promoting an
// internal helper to solver.hpp's public surface; consolidate if a
// third consumer appears in Slice 10+.
// =============================================================================
#include "rbfmax/interpolator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "rbfmax/distance.hpp"
#include "rbfmax/io_json.hpp"

namespace rbfmax {

namespace {

// -----------------------------------------------------------------------------
//  Local polynomial-tail helpers (duplicated from solver.cpp anonymous ns)
// -----------------------------------------------------------------------------

struct MonomialIndex {
    std::vector<int> exponents;
};

void enum_at_total_degree(int remaining_deg, int dim_remaining,
                          std::vector<int>& current,
                          std::vector<MonomialIndex>& out) {
    if (dim_remaining == 1) {
        current.push_back(remaining_deg);
        out.push_back(MonomialIndex{current});
        current.pop_back();
        return;
    }
    for (int k = remaining_deg; k >= 0; --k) {
        current.push_back(k);
        enum_at_total_degree(remaining_deg - k, dim_remaining - 1, current, out);
        current.pop_back();
    }
}

std::vector<MonomialIndex> generate_monomial_basis_local(int dim,
                                                         int max_degree) {
    std::vector<MonomialIndex> out;
    if (dim <= 0 || max_degree < 0) {
        return out;
    }
    std::vector<int> current;
    current.reserve(static_cast<std::size_t>(dim));
    for (int d = 0; d <= max_degree; ++d) {
        enum_at_total_degree(d, dim, current, out);
    }
    return out;
}

Scalar evaluate_monomial_local(const MonomialIndex& m,
                               const Eigen::Ref<const VectorX>& x) noexcept {
    Scalar result = Scalar(1);
    for (std::size_t i = 0; i < m.exponents.size(); ++i) {
        const int e = m.exponents[i];
        const Scalar xi = x(static_cast<Index>(i));
        for (int k = 0; k < e; ++k) {
            result *= xi;
        }
    }
    return result;
}

void build_polynomial_row_local(const VectorX& x, int poly_degree,
                                VectorX& out) noexcept {
    if (poly_degree < 0 || out.size() == 0) {
        return;
    }
    const auto basis = generate_monomial_basis_local(static_cast<int>(x.size()),
                                                     poly_degree);
    const Index Q = static_cast<Index>(basis.size());
    eigen_assert(out.size() == Q && "build_polynomial_row_local: size mismatch");
    for (Index a = 0; a < Q; ++a) {
        out(a) = evaluate_monomial_local(basis[static_cast<std::size_t>(a)], x);
    }
}

}  // namespace

// =============================================================================
//  InterpolatorOptions
// =============================================================================

InterpolatorOptions::InterpolatorOptions() noexcept
    : kernel(),
      poly_degree(-1),
      kdtree_threshold(256),
      knn_neighbors(0),
      force_dense(false) {}

InterpolatorOptions::InterpolatorOptions(const KernelParams& k) noexcept
    : kernel(k),
      poly_degree(minimum_polynomial_degree(k.type)),
      kdtree_threshold(256),
      knn_neighbors(0),
      force_dense(false) {}

// =============================================================================
//  RBFInterpolator
// =============================================================================

RBFInterpolator::RBFInterpolator(const InterpolatorOptions& opts) noexcept
    : opts_(opts),
      fit_result_(),
      fitted_(false),
      kdtree_(nullptr),
      pool_(1, 1, 0),
      indices_buf_(),
      sq_dist_buf_() {
    // pool_ is a 1×1 placeholder; fit() will rebuild it to the correct
    // shape.  We can't construct a 0×0 pool because Eigen's resize-on-
    // assign in predict_with_pool would still allocate; carrying a tiny
    // pool keeps the move/copy semantics simple.
}

RBFInterpolator::RBFInterpolator(RBFInterpolator&& other) noexcept
    : opts_(other.opts_),
      fit_result_(std::move(other.fit_result_)),
      fitted_(other.fitted_),
      kdtree_(std::move(other.kdtree_)),
      pool_(std::move(other.pool_)),
      indices_buf_(std::move(other.indices_buf_)),
      sq_dist_buf_(std::move(other.sq_dist_buf_)) {
    other.fitted_ = false;
}

RBFInterpolator& RBFInterpolator::operator=(RBFInterpolator&& other) noexcept {
    if (this != &other) {
        opts_         = other.opts_;
        fit_result_   = std::move(other.fit_result_);
        fitted_       = other.fitted_;
        kdtree_       = std::move(other.kdtree_);
        pool_         = std::move(other.pool_);
        indices_buf_  = std::move(other.indices_buf_);
        sq_dist_buf_  = std::move(other.sq_dist_buf_);
        other.fitted_ = false;
    }
    return *this;
}

RBFInterpolator RBFInterpolator::clone() const {
    RBFInterpolator copy(opts_);
    copy.fit_result_ = fit_result_;  // Eigen members deep-copy.
    copy.fitted_     = fitted_;
    if (fitted_) {
        copy.pool_ = solver::ScratchPool(copy.fit_result_.centers.cols(),
                                         copy.fit_result_.centers.rows(),
                                         copy.fit_result_.poly_coeffs.rows());
        if (kdtree_) {
            // Critical: the new kd-tree must reference the COPY's centers
            // buffer (not the original's), otherwise the two instances
            // would share spatial storage and violate clone() semantics.
            copy.kdtree_.reset(new spatial::KdTree(copy.fit_result_.centers));
            const Index k = copy.effective_k_neighbors();
            copy.indices_buf_.assign(static_cast<std::size_t>(k), Index{0});
            copy.sq_dist_buf_.assign(static_cast<std::size_t>(k), Scalar{0});
        }
    }
    return copy;
}

// -----------------------------------------------------------------------------
//  Training
// -----------------------------------------------------------------------------

solver::FitStatus RBFInterpolator::fit(
    const Eigen::Ref<const MatrixX>& centers,
    const Eigen::Ref<const MatrixX>& targets,
    Scalar lambda) noexcept {
    const int deg = (opts_.poly_degree >= 0)
                        ? opts_.poly_degree
                        : minimum_polynomial_degree(opts_.kernel.type);
    solver::FitOptions fit_opts(opts_.kernel, deg);

    fit_result_ = solver::fit(centers, targets, fit_opts, lambda);
    fitted_     = (fit_result_.status == solver::FitStatus::OK);

    pool_ = solver::ScratchPool(fit_result_.centers.cols(),
                                fit_result_.centers.rows(),
                                fit_result_.poly_coeffs.rows());

    if (should_use_kdtree()) {
        kdtree_.reset(new spatial::KdTree(fit_result_.centers));
        const Index k = effective_k_neighbors();
        indices_buf_.assign(static_cast<std::size_t>(k), Index{0});
        sq_dist_buf_.assign(static_cast<std::size_t>(k), Scalar{0});
    } else {
        kdtree_.reset();
        indices_buf_.clear();
        sq_dist_buf_.clear();
    }

    return fit_result_.status;
}

solver::FitStatus RBFInterpolator::fit(
    const Eigen::Ref<const MatrixX>& centers,
    const Eigen::Ref<const MatrixX>& targets,
    solver::LambdaAuto) noexcept {
    const int deg = (opts_.poly_degree >= 0)
                        ? opts_.poly_degree
                        : minimum_polynomial_degree(opts_.kernel.type);
    solver::FitOptions fit_opts(opts_.kernel, deg);

    fit_result_ = solver::fit(centers, targets, fit_opts, solver::kLambdaAuto);
    fitted_     = (fit_result_.status == solver::FitStatus::OK);

    pool_ = solver::ScratchPool(fit_result_.centers.cols(),
                                fit_result_.centers.rows(),
                                fit_result_.poly_coeffs.rows());

    if (should_use_kdtree()) {
        kdtree_.reset(new spatial::KdTree(fit_result_.centers));
        const Index k = effective_k_neighbors();
        indices_buf_.assign(static_cast<std::size_t>(k), Index{0});
        sq_dist_buf_.assign(static_cast<std::size_t>(k), Scalar{0});
    } else {
        kdtree_.reset();
        indices_buf_.clear();
        sq_dist_buf_.clear();
    }

    return fit_result_.status;
}

// -----------------------------------------------------------------------------
//  KNN-path predict (Gaussian + N >= threshold)
// -----------------------------------------------------------------------------

VectorX RBFInterpolator::predict_knn(
    const Eigen::Ref<const VectorX>& x) const noexcept {
    const Index k = effective_k_neighbors();
    const Index actual_k = kdtree_->knn_search(
        x, k, indices_buf_.data(), sq_dist_buf_.data());

    const Index M = fit_result_.weights.cols();
    VectorX out = VectorX::Zero(M);

    for (Index i = 0; i < actual_k; ++i) {
        const Index j   = indices_buf_[static_cast<std::size_t>(i)];
        const Scalar r  = std::sqrt(sq_dist_buf_[static_cast<std::size_t>(i)]);
        const Scalar phi = evaluate_kernel(fit_result_.kernel, r);
        out.noalias() += fit_result_.weights.row(j).transpose() * phi;
    }

    if (fit_result_.poly_degree >= 0 && fit_result_.poly_coeffs.rows() > 0) {
        // pool_.query_vec / pool_.poly_vec sized correctly at fit() time.
        pool_.query_vec = x;
        build_polynomial_row_local(pool_.query_vec, fit_result_.poly_degree,
                                   pool_.poly_vec);
        out.noalias() += fit_result_.poly_coeffs.transpose() * pool_.poly_vec;
    }
    return out;
}

// -----------------------------------------------------------------------------
//  Prediction (public)
// -----------------------------------------------------------------------------

Scalar RBFInterpolator::predict_scalar(
    const Eigen::Ref<const VectorX>& x) const noexcept {
    if (!fitted_) {
        return std::numeric_limits<Scalar>::quiet_NaN();
    }
    if (kdtree_) {
        const VectorX y = predict_knn(x);
        return (y.size() > 0) ? y(0)
                              : std::numeric_limits<Scalar>::quiet_NaN();
    }
    return solver::predict_scalar_with_pool(fit_result_, x, pool_);
}

VectorX RBFInterpolator::predict(
    const Eigen::Ref<const VectorX>& x) const noexcept {
    if (!fitted_) {
        const Index M = fit_result_.weights.cols();
        return VectorX::Constant(M, std::numeric_limits<Scalar>::quiet_NaN());
    }
    if (kdtree_) {
        return predict_knn(x);
    }
    return solver::predict_with_pool(fit_result_, x, pool_);
}

MatrixX RBFInterpolator::predict_batch(
    const Eigen::Ref<const MatrixX>& X) const noexcept {
    const Index K = X.rows();
    const Index M = fit_result_.weights.cols();
    if (!fitted_) {
        MatrixX out(K, M);
        out.setConstant(std::numeric_limits<Scalar>::quiet_NaN());
        return out;
    }
    MatrixX out(K, M);
    for (Index i = 0; i < K; ++i) {
        VectorX q = X.row(i).transpose();
        VectorX y = kdtree_
                        ? predict_knn(q)
                        : solver::predict_with_pool(fit_result_, q, pool_);
        out.row(i) = y.transpose();
    }
    return out;
}

// -----------------------------------------------------------------------------
//  State queries
// -----------------------------------------------------------------------------

bool RBFInterpolator::is_fitted() const noexcept { return fitted_; }

solver::FitStatus RBFInterpolator::status() const noexcept {
    return fit_result_.status;
}

solver::SolverPath RBFInterpolator::solver_path() const noexcept {
    return fit_result_.solver_path;
}

Index RBFInterpolator::n_centers() const noexcept {
    return fit_result_.centers.rows();
}

Index RBFInterpolator::dim() const noexcept {
    return fit_result_.centers.cols();
}

Scalar RBFInterpolator::lambda_used() const noexcept {
    return fit_result_.lambda_used;
}

Scalar RBFInterpolator::condition_number() const noexcept {
    return fit_result_.condition_number;
}

bool RBFInterpolator::uses_kdtree() const noexcept {
    return kdtree_ != nullptr;
}

const KernelParams& RBFInterpolator::kernel_params() const noexcept {
    // Returns the kernel inside the FitResult, which is the one actually
    // used for training / loaded from disk.  Before a successful fit or
    // load this still returns a defined object (the default-constructed
    // KernelParams in FitResult's default ctor), so the function itself
    // remains noexcept — callers enforce the "fitted first" contract via
    // is_fitted() rather than this getter.
    return fit_result_.kernel;
}

// -----------------------------------------------------------------------------
//  Persistence (Slice 08) — delegate to rbfmax::io_json
// -----------------------------------------------------------------------------

bool RBFInterpolator::save(const std::string& path) const noexcept {
    return io_json::save(opts_, fit_result_, path);
}

bool RBFInterpolator::load(const std::string& path) noexcept {
    InterpolatorOptions tmp_opts;
    solver::FitResult   tmp_fr;
    if (!io_json::load(tmp_opts, tmp_fr, path)) {
        // Atomic-update contract: interpolator state untouched on failure.
        return false;
    }

    opts_       = tmp_opts;
    fit_result_ = std::move(tmp_fr);
    fitted_     = (fit_result_.status == solver::FitStatus::OK);

    pool_ = solver::ScratchPool(fit_result_.centers.cols(),
                                fit_result_.centers.rows(),
                                fit_result_.poly_coeffs.rows());

    if (should_use_kdtree()) {
        kdtree_.reset(new spatial::KdTree(fit_result_.centers));
        const Index k = effective_k_neighbors();
        indices_buf_.assign(static_cast<std::size_t>(k), Index{0});
        sq_dist_buf_.assign(static_cast<std::size_t>(k), Scalar{0});
    } else {
        kdtree_.reset();
        indices_buf_.clear();
        sq_dist_buf_.clear();
    }
    return true;
}

// -----------------------------------------------------------------------------
//  Private helpers
// -----------------------------------------------------------------------------

bool RBFInterpolator::should_use_kdtree() const noexcept {
    if (!fitted_)                                                  return false;
    if (opts_.force_dense)                                         return false;
    if (fit_result_.centers.rows() < opts_.kdtree_threshold)       return false;
    return opts_.kernel.type == KernelType::kGaussian;
}

Index RBFInterpolator::effective_k_neighbors() const noexcept {
    if (opts_.knn_neighbors > 0) return opts_.knn_neighbors;
    const Index N = fit_result_.centers.rows();
    return (N < Index{32}) ? N : Index{32};
}

}  // namespace rbfmax
