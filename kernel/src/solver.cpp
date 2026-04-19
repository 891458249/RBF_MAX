// =============================================================================
// kernel/src/solver.cpp
// -----------------------------------------------------------------------------
// Implementation of rbfmax::solver — see solver.hpp for the public contract.
//
// Map of this file:
//   1. Anonymous-namespace utilities
//        * polynomial basis enumeration (lex-graded)
//        * input validation
//        * kernel matrix and polynomial matrix builders
//        * symmetric SPD/SPsD solver fallback (LLT → LDLT → BDCSVD)
//        * QR-elimination solver for the augmented [A+λI P; Pᵀ 0] system
//        * GCV lambda selector (SVD closed form)
//   2. Public API
//        * fit(centers, targets, options, lambda)        — fixed λ
//        * fit(centers, targets, options, kLambdaAuto)   — GCV
//        * predict_scalar / predict / predict_batch
// =============================================================================

#include "rbfmax/solver.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include <Eigen/Cholesky>
#include <Eigen/Eigenvalues>
#include <Eigen/QR>
#include <Eigen/SVD>

#include "rbfmax/distance.hpp"

namespace rbfmax {
namespace solver {

namespace {

// =============================================================================
//  Polynomial basis (graded lex multi-indices)
// =============================================================================

/// Multi-index of a single monomial (size = dim).
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

/// All monomials of total degree 0..max_degree in `dim` variables, graded-lex
/// ordered. Empty vector for dim<=0 or max_degree<0.
std::vector<MonomialIndex> generate_monomial_basis(int dim, int max_degree) {
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

/// Evaluate one monomial at `x`. Uses repeated multiplication; max exponent
/// in our supported range (degree ≤ 3) makes this trivially fast.
Scalar evaluate_monomial(const MonomialIndex& m,
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

/// Slice 06 helper — write the polynomial-tail row vector P_α(x) into a
/// caller-supplied output buffer.  Mirrors the column producer in
/// build_polynomial_matrix but for a single query point.  `out` must be
/// pre-sized to the basis cardinality; when sizes match no allocation
/// occurs.  The basis enumeration itself still allocates a small
/// std::vector<MonomialIndex> — a once-per-call cost (not per inner loop)
/// that Slice 09 will revisit if benchmarks demand it.
void build_polynomial_row(const VectorX& x, int poly_degree,
                          VectorX& out) noexcept {
    if (poly_degree < 0 || out.size() == 0) {
        return;
    }
    const auto basis = generate_monomial_basis(static_cast<int>(x.size()),
                                               poly_degree);
    const Index Q = static_cast<Index>(basis.size());
    eigen_assert(out.size() == Q && "build_polynomial_row: size mismatch");
    for (Index a = 0; a < Q; ++a) {
        out(a) = evaluate_monomial(basis[static_cast<std::size_t>(a)], x);
    }
}

// =============================================================================
//  Input validation
// =============================================================================

FitStatus validate_inputs(const Eigen::Ref<const MatrixX>& centers,
                          const Eigen::Ref<const MatrixX>& targets,
                          const FitOptions& options) noexcept {
    if (centers.rows() == 0) return FitStatus::INSUFFICIENT_SAMPLES;
    if (centers.cols() == 0) return FitStatus::INVALID_INPUT;
    if (targets.rows() != centers.rows()) return FitStatus::INVALID_INPUT;
    if (targets.cols() == 0) return FitStatus::INVALID_INPUT;
    if (!centers.allFinite()) return FitStatus::INVALID_INPUT;
    if (!targets.allFinite()) return FitStatus::INVALID_INPUT;

    if (options.poly_degree >= 0) {
        const auto basis = generate_monomial_basis(
            static_cast<int>(centers.cols()), options.poly_degree);
        if (static_cast<Index>(basis.size()) > centers.rows()) {
            return FitStatus::INSUFFICIENT_SAMPLES;
        }
    }
    return FitStatus::OK;
}

// =============================================================================
//  Matrix builders
// =============================================================================

MatrixX build_kernel_matrix(const Eigen::Ref<const MatrixX>& centers,
                            const KernelParams& kernel) noexcept {
    const Index N = centers.rows();
    MatrixX A(N, N);
    for (Index i = 0; i < N; ++i) {
        A(i, i) = evaluate_kernel(kernel, Scalar(0));
        for (Index j = i + 1; j < N; ++j) {
            const Scalar r = std::sqrt(metric::squared_distance(
                centers.row(i), centers.row(j)));
            const Scalar k = evaluate_kernel(kernel, r);
            A(i, j) = k;
            A(j, i) = k;
        }
    }
    return A;
}

MatrixX build_polynomial_matrix(const Eigen::Ref<const MatrixX>& centers,
                                int poly_degree) noexcept {
    const Index N = centers.rows();
    if (poly_degree < 0) {
        return MatrixX(N, 0);
    }
    const auto basis = generate_monomial_basis(
        static_cast<int>(centers.cols()), poly_degree);
    MatrixX P(N, static_cast<Index>(basis.size()));
    for (Index i = 0; i < N; ++i) {
        VectorX row = centers.row(i).transpose();
        for (std::size_t j = 0; j < basis.size(); ++j) {
            P(i, static_cast<Index>(j)) = evaluate_monomial(basis[j], row);
        }
    }
    return P;
}

// =============================================================================
//  Symmetric solver fallback chain (LLT → LDLT → BDCSVD)
// =============================================================================
//
// `A_reg` is expected to be symmetric (positive (semi)definite for the LLT
// branch).  Returns true on success and writes the solver path & condition
// number through the out-params.
//
bool solve_symmetric_system(const MatrixX& A_reg, const MatrixX& Y,
                            MatrixX& out_X, SolverPath& out_path,
                            Scalar& out_condition_number) noexcept {
    out_condition_number = Scalar(-1);
    try {
        Eigen::LLT<MatrixX> llt(A_reg);
        if (llt.info() == Eigen::Success) {
            out_X = llt.solve(Y);
            if (out_X.allFinite()) {
                out_path = SolverPath::LLT;
                return true;
            }
        }

        Eigen::LDLT<MatrixX> ldlt(A_reg);
        if (ldlt.info() == Eigen::Success) {
            out_X = ldlt.solve(Y);
            if (out_X.allFinite()) {
                out_path = SolverPath::LDLT;
                return true;
            }
        }

        Eigen::BDCSVD<MatrixX> svd(A_reg, Eigen::ComputeThinU | Eigen::ComputeThinV);
        out_X = svd.solve(Y);
        if (!out_X.allFinite()) {
            out_path = SolverPath::FAILED;
            return false;
        }
        const VectorX& sv = svd.singularValues();
        if (sv.size() > 0 && sv(sv.size() - 1) > Scalar(0)) {
            out_condition_number = sv(0) / sv(sv.size() - 1);
        }
        out_path = SolverPath::BDCSVD;
        return true;
    } catch (...) {
        out_path = SolverPath::FAILED;
        return false;
    }
}

// =============================================================================
//  QR-elimination for augmented system
//      [A + λI   P  ] [w]   [y]
//      [   Pᵀ   0  ] [v] = [0]
//  See math_derivation.md §13.
// =============================================================================

bool solve_with_qr_elimination(const MatrixX& A_reg, const MatrixX& P,
                               const MatrixX& Y, MatrixX& out_weights,
                               MatrixX& out_poly_coeffs, SolverPath& out_path,
                               Scalar& out_condition_number) noexcept {
    out_condition_number = Scalar(-1);
    const Index N = A_reg.rows();
    const Index Q = P.cols();
    if (Q <= 0) {
        // No poly tail — caller should have routed to solve_symmetric_system.
        out_path = SolverPath::FAILED;
        return false;
    }
    if (Q >= N) {
        // Cannot eliminate; would leave zero degrees of freedom for w.
        out_path = SolverPath::FAILED;
        return false;
    }

    try {
        Eigen::HouseholderQR<MatrixX> qr(P);
        // Q_full = N×N orthogonal; first Q cols = Q1, last (N-Q) cols = Q2.
        const MatrixX Q_full = qr.householderQ() * MatrixX::Identity(N, N);
        const MatrixX Q1 = Q_full.leftCols(Q);
        const MatrixX Q2 = Q_full.rightCols(N - Q);
        // R is the top Q×Q upper-triangular block.
        const MatrixX R =
            qr.matrixQR().topLeftCorner(Q, Q).triangularView<Eigen::Upper>();

        // Project onto null(Pᵀ): A_sub = Q2ᵀ (A+λI) Q2 is (N-Q)×(N-Q) SPD.
        const MatrixX A_sub = Q2.transpose() * A_reg * Q2;
        const MatrixX rhs_sub = Q2.transpose() * Y;

        MatrixX U;  // (N-Q) × M
        SolverPath sub_path = SolverPath::FAILED;
        Scalar sub_cond = Scalar(-1);
        if (!solve_symmetric_system(A_sub, rhs_sub, U, sub_path, sub_cond)) {
            out_path = SolverPath::FAILED;
            return false;
        }

        out_weights = Q2 * U;  // N × M

        // Recover v from P v = y - (A+λI) w  ⇒  R v = Q1ᵀ (y - (A+λI) w).
        const MatrixX residual = Y - A_reg * out_weights;
        const MatrixX rhs_v = Q1.transpose() * residual;
        out_poly_coeffs =
            R.triangularView<Eigen::Upper>().solve(rhs_v);  // Q × M

        if (!out_weights.allFinite() || !out_poly_coeffs.allFinite()) {
            out_path = SolverPath::FAILED;
            return false;
        }

        out_path = sub_path;          // Inherit path from inner solve.
        out_condition_number = sub_cond;
        return true;
    } catch (...) {
        out_path = SolverPath::FAILED;
        return false;
    }
}

// =============================================================================
//  GCV lambda selector (SVD closed form)
// =============================================================================
//
// Operates on the kernel matrix A only (no poly tail — see §12.3 caveat).
// Returns the lambda from a 50-point log-uniform grid in [1e-10, 1e-2] that
// minimises the GCV objective; if every candidate is pathological (NaN /
// non-positive denominator), returns the conservative default 1e-6.
//
Scalar select_lambda_gcv(const VectorX& evals, const MatrixX& evecs,
                         const VectorX& y_col) noexcept {
    const Index N = evals.size();
    if (N == 0) {
        return Scalar(1e-6);
    }
    const VectorX uTy = evecs.transpose() * y_col;

    constexpr int kGridSize = 50;
    constexpr double kLogLo = -10.0;
    constexpr double kLogHi = -2.0;

    Scalar best_lambda = Scalar(1e-6);
    Scalar best_gcv = std::numeric_limits<Scalar>::infinity();
    bool found_any = false;

    for (int g = 0; g < kGridSize; ++g) {
        const double log_lambda = kLogLo +
            (kLogHi - kLogLo) * static_cast<double>(g) /
                static_cast<double>(kGridSize - 1);
        const Scalar lambda = static_cast<Scalar>(std::pow(10.0, log_lambda));

        Scalar trace_H = Scalar(0);
        Scalar res_norm_sq = Scalar(0);
        for (Index i = 0; i < N; ++i) {
            const Scalar denom = evals(i) + lambda;
            if (denom <= Scalar(0) || !std::isfinite(denom)) {
                trace_H = std::numeric_limits<Scalar>::quiet_NaN();
                break;
            }
            trace_H += evals(i) / denom;
            const Scalar shrink = lambda / denom;
            res_norm_sq += shrink * shrink * uTy(i) * uTy(i);
        }
        if (!std::isfinite(trace_H)) continue;
        const Scalar denom_outer =
            static_cast<Scalar>(N) - trace_H;
        if (denom_outer <= Scalar(0) || !std::isfinite(denom_outer)) continue;
        const Scalar gcv =
            static_cast<Scalar>(N) * res_norm_sq /
            (denom_outer * denom_outer);
        if (!std::isfinite(gcv)) continue;
        if (!found_any || gcv < best_gcv) {
            best_gcv = gcv;
            best_lambda = lambda;
            found_any = true;
        }
    }
    return best_lambda;
}

// =============================================================================
//  Common fit driver (already-validated inputs, lambda already clamped)
// =============================================================================

FitResult do_fit(const Eigen::Ref<const MatrixX>& centers,
                 const Eigen::Ref<const MatrixX>& targets,
                 const FitOptions& options, Scalar lambda) noexcept {
    FitResult fr;
    fr.kernel = options.kernel;
    fr.poly_degree = options.poly_degree;
    fr.lambda_used = lambda;
    fr.centers = centers;  // Self-contained copy.

    try {
        const Index N = centers.rows();
        const MatrixX A = build_kernel_matrix(centers, options.kernel);

        // A_reg = A + lambda * I.
        MatrixX A_reg = A;
        for (Index i = 0; i < N; ++i) {
            A_reg(i, i) += lambda;
        }

        const MatrixX P = build_polynomial_matrix(centers, options.poly_degree);

        SolverPath path = SolverPath::FAILED;
        Scalar cond = Scalar(-1);
        bool ok = false;
        if (P.cols() > 0) {
            ok = solve_with_qr_elimination(A_reg, P, targets, fr.weights,
                                           fr.poly_coeffs, path, cond);
        } else {
            fr.poly_coeffs = MatrixX(0, targets.cols());
            ok = solve_symmetric_system(A_reg, targets, fr.weights, path,
                                        cond);
        }

        fr.solver_path = path;
        fr.condition_number = cond;

        if (!ok) {
            fr.status = FitStatus::SINGULAR_MATRIX;
            return fr;
        }

        // Residual ||A w + P v - y||_F / ||y||_F.
        MatrixX residual = A * fr.weights - targets;
        if (P.cols() > 0) {
            residual.noalias() += P * fr.poly_coeffs;
        }
        const Scalar y_norm = targets.norm();
        fr.residual_norm =
            (y_norm > Scalar(0)) ? (residual.norm() / y_norm) : residual.norm();
        fr.status = FitStatus::OK;
        return fr;
    } catch (...) {
        fr.status = FitStatus::INVALID_INPUT;
        fr.solver_path = SolverPath::FAILED;
        fr.condition_number = Scalar(-1);
        return fr;
    }
}

}  // namespace

// =============================================================================
//  ScratchPool — pre-allocated buffers (Slice 06)
// =============================================================================

ScratchPool::ScratchPool(Index dim, Index n_centers, Index poly_cols) noexcept
    : query_vec(dim),
      kernel_vals(n_centers),
      diff_vec(dim),
      poly_vec(poly_cols) {
    // Eigen VectorX resize at construction; subsequent predict_with_pool
    // writes into these without allocating provided shapes match.
}

// =============================================================================
//  Public API
// =============================================================================

FitResult fit(const Eigen::Ref<const MatrixX>& centers,
              const Eigen::Ref<const MatrixX>& targets,
              const FitOptions& options, Scalar lambda) noexcept {
    FitResult fr;
    fr.kernel = options.kernel;
    fr.poly_degree = options.poly_degree;

    const FitStatus pre = validate_inputs(centers, targets, options);
    if (pre != FitStatus::OK) {
        fr.status = pre;
        fr.solver_path = SolverPath::FAILED;
        return fr;
    }

    // Silent clamp + Debug assert.
    if (lambda < kLambdaMin) {
        eigen_assert(lambda >= kLambdaMin &&
                     "lambda below 1e-12 will likely fail LLT");
        lambda = kLambdaMin;
    }
    if (!std::isfinite(lambda)) {
        fr.status = FitStatus::INVALID_INPUT;
        fr.solver_path = SolverPath::FAILED;
        return fr;
    }
    return do_fit(centers, targets, options, lambda);
}

FitResult fit(const Eigen::Ref<const MatrixX>& centers,
              const Eigen::Ref<const MatrixX>& targets,
              const FitOptions& options, LambdaAuto) noexcept {
    FitResult fr;
    fr.kernel = options.kernel;
    fr.poly_degree = options.poly_degree;

    const FitStatus pre = validate_inputs(centers, targets, options);
    if (pre != FitStatus::OK) {
        fr.status = pre;
        fr.solver_path = SolverPath::FAILED;
        return fr;
    }

    Scalar selected = Scalar(1e-6);
    try {
        const MatrixX A = build_kernel_matrix(centers, options.kernel);
        Eigen::SelfAdjointEigenSolver<MatrixX> eig(A);
        if (eig.info() == Eigen::Success) {
            // GCV on the first target column for determinism (per §12.3).
            const VectorX y0 = targets.col(0);
            selected = select_lambda_gcv(eig.eigenvalues(), eig.eigenvectors(),
                                         y0);
        }
    } catch (...) {
        // Keep default 1e-6.
    }
    if (selected < kLambdaMin) {
        selected = kLambdaMin;
    }
    return do_fit(centers, targets, options, selected);
}

// -----------------------------------------------------------------------------
//  Pool-explicit overloads — the canonical compute path since Slice 06.
//  predict / predict_scalar / predict_batch all delegate here so that the
//  arithmetic path is single-sourced (P4/P5 bit-identity property).
// -----------------------------------------------------------------------------

Scalar predict_scalar_with_pool(const FitResult& fr,
                                const Eigen::Ref<const VectorX>& x,
                                ScratchPool& pool) noexcept {
    if (fr.status != FitStatus::OK) {
        return std::numeric_limits<Scalar>::quiet_NaN();
    }
    if (fr.weights.cols() == 0 || fr.weights.rows() == 0) {
        return Scalar(0);
    }
    eigen_assert(x.size() == fr.centers.cols());
    eigen_assert(pool.dim() == fr.centers.cols() &&
                 "ScratchPool dim does not match FitResult");
    eigen_assert(pool.n_centers() == fr.centers.rows() &&
                 "ScratchPool n_centers does not match FitResult");
    try {
        const Index N = fr.centers.rows();
        pool.query_vec = x;
        Scalar acc = Scalar(0);
        for (Index j = 0; j < N; ++j) {
            pool.diff_vec = pool.query_vec - fr.centers.row(j).transpose();
            const Scalar r = pool.diff_vec.norm();
            const Scalar k = evaluate_kernel(fr.kernel, r);
            pool.kernel_vals(j) = k;
            acc += fr.weights(j, 0) * k;
        }
        if (fr.poly_degree >= 0 && fr.poly_coeffs.rows() > 0) {
            eigen_assert(pool.poly_cols() == fr.poly_coeffs.rows() &&
                         "ScratchPool poly_cols does not match FitResult");
            build_polynomial_row(pool.query_vec, fr.poly_degree,
                                 pool.poly_vec);
            for (Index a = 0; a < pool.poly_vec.size(); ++a) {
                acc += fr.poly_coeffs(a, 0) * pool.poly_vec(a);
            }
        }
        return acc;
    } catch (...) {
        return std::numeric_limits<Scalar>::quiet_NaN();
    }
}

VectorX predict_with_pool(const FitResult& fr,
                          const Eigen::Ref<const VectorX>& x,
                          ScratchPool& pool) noexcept {
    if (fr.status != FitStatus::OK) {
        return VectorX();
    }
    const Index M = fr.weights.cols();
    if (M == 0) {
        return VectorX(0);
    }
    eigen_assert(x.size() == fr.centers.cols());
    eigen_assert(pool.dim() == fr.centers.cols() &&
                 "ScratchPool dim does not match FitResult");
    eigen_assert(pool.n_centers() == fr.centers.rows() &&
                 "ScratchPool n_centers does not match FitResult");
    try {
        const Index N = fr.centers.rows();
        pool.query_vec = x;
        for (Index j = 0; j < N; ++j) {
            pool.diff_vec = pool.query_vec - fr.centers.row(j).transpose();
            const Scalar r = pool.diff_vec.norm();
            pool.kernel_vals(j) = evaluate_kernel(fr.kernel, r);
        }
        VectorX out = fr.weights.transpose() * pool.kernel_vals;  // M-vector
        if (fr.poly_degree >= 0 && fr.poly_coeffs.rows() > 0) {
            eigen_assert(pool.poly_cols() == fr.poly_coeffs.rows() &&
                         "ScratchPool poly_cols does not match FitResult");
            build_polynomial_row(pool.query_vec, fr.poly_degree,
                                 pool.poly_vec);
            out.noalias() += fr.poly_coeffs.transpose() * pool.poly_vec;
        }
        return out;
    } catch (...) {
        return VectorX();
    }
}

// -----------------------------------------------------------------------------
//  Convenience overloads — internally allocate a temporary pool and delegate.
//  Slice 06 unifies the compute path so predict()/predict_scalar() and
//  predict_with_pool()/predict_scalar_with_pool() are bit-identical for the
//  same FitResult+query pair.
// -----------------------------------------------------------------------------

Scalar predict_scalar(const FitResult& fr,
                      const Eigen::Ref<const VectorX>& x) noexcept {
    if (fr.status != FitStatus::OK) {
        return std::numeric_limits<Scalar>::quiet_NaN();
    }
    try {
        ScratchPool pool(fr.centers.cols(), fr.centers.rows(),
                         fr.poly_coeffs.rows());
        return predict_scalar_with_pool(fr, x, pool);
    } catch (...) {
        return std::numeric_limits<Scalar>::quiet_NaN();
    }
}

VectorX predict(const FitResult& fr,
                const Eigen::Ref<const VectorX>& x) noexcept {
    if (fr.status != FitStatus::OK) {
        return VectorX();
    }
    try {
        ScratchPool pool(fr.centers.cols(), fr.centers.rows(),
                         fr.poly_coeffs.rows());
        return predict_with_pool(fr, x, pool);
    } catch (...) {
        return VectorX();
    }
}

MatrixX predict_batch(const FitResult& fr,
                      const Eigen::Ref<const MatrixX>& X) noexcept {
    const Index K = X.rows();
    if (fr.status != FitStatus::OK) {
        return MatrixX(K, 0);
    }
    const Index M = fr.weights.cols();
    if (K == 0) {
        return MatrixX(0, M);
    }
    try {
        MatrixX out(K, M);
        ScratchPool pool(fr.centers.cols(), fr.centers.rows(),
                         fr.poly_coeffs.rows());
        for (Index i = 0; i < K; ++i) {
            VectorX y = predict_with_pool(fr, X.row(i).transpose(), pool);
            out.row(i) = y.transpose();
        }
        return out;
    } catch (...) {
        return MatrixX(K, M);
    }
}

}  // namespace solver
}  // namespace rbfmax
