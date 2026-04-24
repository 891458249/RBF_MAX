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
#include "rbfmax/quaternion.hpp"

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

// =============================================================================
//  Heterogeneous input validation (Slice 17A)
// =============================================================================
//
//  Checks the heterogeneous-fit invariants before the compositing pipeline
//  runs.  Callers receive the status back via FitResult::status; no exceptions
//  are thrown.
//
FitStatus validate_composite_inputs(
    const Eigen::Ref<const MatrixX>& scalar_centers,
    const std::vector<MatrixX>& quat_features,
    const Eigen::Ref<const MatrixX>& targets,
    const FitOptions& options,
    const FeatureSpec& spec) noexcept {
    // Block count must match spec.
    if (quat_features.size() != spec.quat_blocks.size()) {
        return FitStatus::INVALID_INPUT;
    }

    // A fully-empty spec (no scalars AND no quats) is degenerate.
    if (spec.scalar_dim == 0 && spec.quat_blocks.empty()) {
        return FitStatus::INVALID_INPUT;
    }

    // Infer sample count from whichever block is populated.
    Index N = 0;
    if (spec.scalar_dim > 0) {
        N = scalar_centers.rows();
        if (scalar_centers.cols() != spec.scalar_dim) {
            return FitStatus::INVALID_INPUT;
        }
    } else if (!quat_features.empty()) {
        N = quat_features.front().rows();
    }
    if (N == 0) {
        return FitStatus::INSUFFICIENT_SAMPLES;
    }

    if (targets.rows() != N) return FitStatus::INVALID_INPUT;
    if (targets.cols() == 0) return FitStatus::INVALID_INPUT;
    if (!targets.allFinite()) return FitStatus::INVALID_INPUT;
    if (spec.scalar_dim > 0 && !scalar_centers.allFinite()) {
        return FitStatus::INVALID_INPUT;
    }

    // Each quat block must be N × 4 with unit-length rows (within tolerance).
    for (std::size_t k = 0; k < quat_features.size(); ++k) {
        const MatrixX& qf = quat_features[k];
        if (qf.rows() != N) return FitStatus::INVALID_INPUT;
        if (qf.cols() != 4) return FitStatus::INVALID_INPUT;
        if (!qf.allFinite()) return FitStatus::INVALID_INPUT;
        for (Index i = 0; i < N; ++i) {
            const Scalar n2 = qf.row(i).squaredNorm();
            if (!std::isfinite(n2)) return FitStatus::INVALID_INPUT;
            // Tolerance kQuatIdentityEps=1e-14 on squared norm (≈ 1e-7 on norm).
            if (std::abs(n2 - Scalar(1)) > Scalar(1e-6)) {
                return FitStatus::INVALID_INPUT;
            }
        }
        // Axis must be unit length for Swing/Twist/SwingTwist.
        if (spec.quat_blocks[k].space != SolverSpace::Full) {
            const Scalar axn2 = spec.quat_blocks[k].axis.squaredNorm();
            if (!std::isfinite(axn2)) return FitStatus::INVALID_INPUT;
            if (std::abs(axn2 - Scalar(1)) > Scalar(1e-6)) {
                return FitStatus::INVALID_INPUT;
            }
        }
    }

    // Polynomial-degree sufficiency: Slice 17A keeps classical basis over the
    // scalar centers; heterogeneous polynomial terms are out of scope here.
    if (options.poly_degree >= 0 && spec.scalar_dim > 0) {
        const auto basis = generate_monomial_basis(
            static_cast<int>(spec.scalar_dim), options.poly_degree);
        if (static_cast<Index>(basis.size()) > N) {
            return FitStatus::INSUFFICIENT_SAMPLES;
        }
    }
    return FitStatus::OK;
}

// =============================================================================
//  Composite distance-matrix builder (Slice 17A)
// =============================================================================
//
//  Replicates the cmt setFeatures pipeline (linearRegressionSolver.cpp:20-132)
//  for heterogeneous scalar + quaternion RBF inputs.  Pure 17A scope — no
//  one-hot θ switch, no persisted sample_radii in FitResult (Decision 4).
//
//  Column layout (matches cmt where applicable; Full-mode is a 17A design
//  choice — see plan Drift #3):
//    * Scalar block (cols 0 .. N-1)  when scalar_dim > 0
//        m.col(i) = || X_normalised.row(i) - X_normalised.row(j) || for all j
//        (i.e. column i = distances from every sample to sample i)
//        Then scalar block is Frobenius-normalised by its own norm and RBFed
//        with the global kernel.
//    * Quat block k (cols blockOffset[k] .. blockOffset[k] + cpp*N - 1),
//      cpp = FeatureSpec::cols_per_pose(space):
//        Full         → m(s1, blockOffset[k] + s)         = geo(q_s1, q_s)
//        Swing        → m(s1, blockOffset[k] + 2s)        = swing_dist(...)
//                       m(s1, blockOffset[k] + 2s + 1)    = 0
//        Twist        → m(s1, blockOffset[k] + 2s)        = 0
//                       m(s1, blockOffset[k] + 2s + 1)    = twist_dist(...)
//        SwingTwist   → both distances filled
//      Per-sample adaptive radius tracking applies to all four modes (plan
//      Step 3.4 constraint #4): sample_radii[s] = min non-trivial distance
//      across ALL quat blocks involving sample s.  RBF is then applied per
//      training-pose column group using sample_radii[s].
//
//  Returns false on NaN / overflow in the pipeline; true on success.  The
//  caller's out-params are in a partially-filled state on false return —
//  callers must wrap the failure in FitStatus::INVALID_INPUT at the
//  FitResult level.
//
bool build_composite_distance_matrix(
        const Eigen::Ref<const MatrixX>& scalar_centers,
        const std::vector<MatrixX>& quat_features,
        const FeatureSpec& spec,
        const KernelParams& kernel,
        MatrixX& out_X_scalar_normalised,
        VectorX& out_feature_norms,
        Scalar& out_distance_norm,
        VectorX& out_sample_radii,
        MatrixX& out_M) noexcept {
    try {
        const Index N = spec.scalar_dim > 0
                            ? scalar_centers.rows()
                            : (quat_features.empty() ? Index(0)
                                                     : quat_features.front().rows());
        if (N == 0) return false;

        const Index total_cols = spec.total_distance_columns(N);
        out_M = MatrixX::Zero(N, total_cols);

        // -------------------------------------------------------------------
        // Step 1-4 — scalar block: per-column L2 → pairwise distance →
        // Frobenius → RBF.  Only if scalar_dim > 0.
        // -------------------------------------------------------------------
        const Index scalar_cols = (spec.scalar_dim > 0) ? N : Index(0);
        out_X_scalar_normalised =
            (spec.scalar_dim > 0) ? MatrixX(scalar_centers) : MatrixX();
        out_feature_norms =
            (spec.scalar_dim > 0) ? VectorX::Zero(spec.scalar_dim) : VectorX();
        out_distance_norm = Scalar(0);

        if (spec.scalar_dim > 0) {
            // Step 1 — per-column L2 normalise.
            for (Index j = 0; j < spec.scalar_dim; ++j) {
                const Scalar n = out_X_scalar_normalised.col(j).norm();
                out_feature_norms(j) = n;
                if (n != Scalar(0)) {
                    out_X_scalar_normalised.col(j) /= n;
                }
            }

            // Step 2 — pairwise distance columns (cmt L56-58 convention).
            for (Index i = 0; i < N; ++i) {
                for (Index r = 0; r < N; ++r) {
                    out_M(r, i) = (out_X_scalar_normalised.row(r) -
                                    out_X_scalar_normalised.row(i))
                                      .norm();
                }
            }

            // Step 3 — Frobenius-normalise the scalar block (cmt L60-62).
            out_distance_norm =
                out_M.block(0, 0, N, scalar_cols).norm();
            if (out_distance_norm > Scalar(0)) {
                out_M.block(0, 0, N, scalar_cols) /= out_distance_norm;
            }

            // Step 4 — applyRbf on scalar block with global radius
            //   effective_r = distance  (rbfmax global radius = 1 convention;
            //   KernelParams.eps carries the shape parameter for kernels that
            //   consume one).
            for (Index r = 0; r < N; ++r) {
                for (Index c = 0; c < scalar_cols; ++c) {
                    out_M(r, c) = evaluate_kernel(kernel, out_M(r, c));
                }
            }
        }

        if (quat_features.empty()) {
            out_sample_radii = VectorX();
            return true;
        }

        // -------------------------------------------------------------------
        // Step 5 — build raw per-block distance matrices, track sample_radii.
        // -------------------------------------------------------------------
        const std::size_t B = quat_features.size();
        std::vector<MatrixX> mQuat(B);
        for (std::size_t k = 0; k < B; ++k) {
            const Index cpp = FeatureSpec::cols_per_pose(spec.quat_blocks[k].space);
            mQuat[k] = MatrixX::Zero(N, N * cpp);
        }

        // Pre-decompose each training quat per block for Swing/Twist modes —
        // avoids redundant decomposition across the O(N²) pair loop.
        std::vector<std::vector<Quaternion>> swings(B), twists(B);
        std::vector<std::vector<Quaternion>> full_quats(B);
        for (std::size_t k = 0; k < B; ++k) {
            const SolverSpace space = spec.quat_blocks[k].space;
            const Vector3& axis = spec.quat_blocks[k].axis;
            if (space == SolverSpace::Full) {
                full_quats[k].resize(static_cast<std::size_t>(N));
                for (Index s = 0; s < N; ++s) {
                    full_quats[k][static_cast<std::size_t>(s)] = Quaternion(
                        quat_features[k](s, 3),  // w
                        quat_features[k](s, 0),  // x
                        quat_features[k](s, 1),  // y
                        quat_features[k](s, 2)); // z
                }
            } else {
                swings[k].resize(static_cast<std::size_t>(N));
                twists[k].resize(static_cast<std::size_t>(N));
                for (Index s = 0; s < N; ++s) {
                    const Quaternion q(quat_features[k](s, 3),
                                       quat_features[k](s, 0),
                                       quat_features[k](s, 1),
                                       quat_features[k](s, 2));
                    const rotation::SwingTwist st =
                        rotation::decompose_swing_twist(q, axis);
                    swings[k][static_cast<std::size_t>(s)] = st.swing;
                    twists[k][static_cast<std::size_t>(s)] = st.twist;
                }
            }
        }

        out_sample_radii = VectorX::Ones(N);
        const Scalar kSampleEps = Scalar(1e-6);

        for (std::size_t k = 0; k < B; ++k) {
            const SolverSpace space = spec.quat_blocks[k].space;
            const Index cpp = FeatureSpec::cols_per_pose(space);
            for (Index s1 = 0; s1 < N; ++s1) {
                for (Index s2 = 0; s2 < N; ++s2) {
                    if (space == SolverSpace::Full) {
                        const Scalar d = metric::quaternion_geodesic_distance(
                            full_quats[k][static_cast<std::size_t>(s1)],
                            full_quats[k][static_cast<std::size_t>(s2)]);
                        mQuat[k](s1, s2) = d;
                        if (d > kSampleEps && d < out_sample_radii(s1)) {
                            out_sample_radii(s1) = d;
                        }
                    } else {
                        const Scalar swing_d = metric::quaternion_geodesic_distance(
                            swings[k][static_cast<std::size_t>(s1)],
                            swings[k][static_cast<std::size_t>(s2)]);
                        const Scalar twist_d = metric::quaternion_geodesic_distance(
                            twists[k][static_cast<std::size_t>(s1)],
                            twists[k][static_cast<std::size_t>(s2)]);

                        const Scalar emit_swing =
                            (space == SolverSpace::Twist) ? Scalar(0) : swing_d;
                        const Scalar emit_twist =
                            (space == SolverSpace::Swing) ? Scalar(0) : twist_d;

                        mQuat[k](s1, s2 * 2)     = emit_swing;
                        mQuat[k](s1, s2 * 2 + 1) = emit_twist;

                        if (emit_swing > kSampleEps &&
                            emit_swing < out_sample_radii(s1)) {
                            out_sample_radii(s1) = emit_swing;
                        }
                        if (emit_twist > kSampleEps &&
                            emit_twist < out_sample_radii(s1)) {
                            out_sample_radii(s1) = emit_twist;
                        }
                    }
                }
            }

            // Step 6 — per-training-pose RBF, using sample_radii[pose].
            // rbfmax convention: "effective radius" R scales the distance
            // argument before the kernel, i.e. φ(raw/R).  This generalises
            // cmt's Gaussian(radius*R) to every rbfmax kernel type uniformly.
            for (Index pose = 0; pose < N; ++pose) {
                const Scalar R = out_sample_radii(pose);
                const Scalar R_eff = (R > Scalar(0)) ? R : Scalar(1);
                if (space == SolverSpace::Full) {
                    for (Index r = 0; r < N; ++r) {
                        const Scalar raw = mQuat[k](r, pose);
                        mQuat[k](r, pose) =
                            evaluate_kernel(kernel, raw / R_eff);
                    }
                } else {
                    for (Index r = 0; r < N; ++r) {
                        const Scalar raw_s = mQuat[k](r, pose * 2);
                        const Scalar raw_t = mQuat[k](r, pose * 2 + 1);
                        mQuat[k](r, pose * 2) =
                            evaluate_kernel(kernel, raw_s / R_eff);
                        mQuat[k](r, pose * 2 + 1) =
                            evaluate_kernel(kernel, raw_t / R_eff);
                    }
                }
            }

            // Insert this block into the main matrix at the right offset.
            Index block_offset = scalar_cols;
            for (std::size_t kk = 0; kk < k; ++kk) {
                block_offset +=
                    FeatureSpec::cols_per_pose(spec.quat_blocks[kk].space) * N;
            }
            out_M.block(0, block_offset, N, N * cpp) = mQuat[k];
        }

        if (!out_M.allFinite()) return false;
        return true;
    } catch (...) {
        return false;
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

// =============================================================================
//  Heterogeneous fit (Slice 17A) — scalar + quaternion input blocks
// =============================================================================
//
//  Scalar-only dispatch branch — `spec.is_scalar_only() && quat_features.empty()`
//  — explicitly returns legacy fit(scalar_centers, targets, options, λ).  The
//  legacy function body is not refactored; the four new FitResult tail fields
//  (feature_spec / quat_features / feature_norms / distance_norm) remain
//  default-constructed in the scalar-only path.  This invariant is verified by
//  the 17A-SCALAR-ORACLE test group.
//
//  Heterogeneous branch (Slice 17A phase-in) — validates shapes; full cmt
//  composite pipeline lands in the second 17A commit (build_composite_distance
//  _matrix + θ solve).  Until then, non-scalar-only calls return
//  FitStatus::INVALID_INPUT with solver_path=FAILED and spec snapshot on
//  the result so callers can tell a true rejection from the placeholder.
//
FitResult fit(const Eigen::Ref<const MatrixX>& scalar_centers,
              const std::vector<MatrixX>& quat_features,
              const Eigen::Ref<const MatrixX>& targets,
              const FitOptions& options,
              const FeatureSpec& spec,
              Scalar lambda) noexcept {
    // Scalar-only dispatch — 17A-SCALAR-ORACLE invariant.
    //
    // The legacy fit() does not know about FeatureSpec, so we overlay the
    // caller's spec onto its returned FitResult.  The 10 pre-17A fields
    // pass through byte-identically; the other 3 new tail fields
    // (quat_features, feature_norms, distance_norm) stay default-
    // constructed because legacy fit() returns a FitResult built from
    // the default constructor (which our Slice 17A edit initialises them
    // to empty / 0).  The overlay assignment is pure post-processing:
    // it does not touch the arithmetic path.
    if (spec.is_scalar_only() && quat_features.empty()) {
        FitResult fr = fit(scalar_centers, targets, options, lambda);
        fr.feature_spec = spec;
        return fr;
    }

    FitResult fr;
    fr.kernel = options.kernel;
    fr.poly_degree = options.poly_degree;
    fr.feature_spec = spec;

    const FitStatus pre =
        validate_composite_inputs(scalar_centers, quat_features, targets,
                                  options, spec);
    if (pre != FitStatus::OK) {
        fr.status = pre;
        fr.solver_path = SolverPath::FAILED;
        return fr;
    }

    if (!std::isfinite(lambda)) {
        fr.status = FitStatus::INVALID_INPUT;
        fr.solver_path = SolverPath::FAILED;
        return fr;
    }
    if (lambda < kLambdaMin) {
        lambda = kLambdaMin;
    }

    // -----------------------------------------------------------------------
    // Composite distance-matrix pipeline (cmt setFeatures steps 1-6).  On
    // failure we surface INVALID_INPUT + FAILED solver_path; caller can
    // distinguish this from INSUFFICIENT_SAMPLES via the pre-check above.
    // -----------------------------------------------------------------------
    MatrixX X_norm;
    VectorX feature_norms;
    Scalar distance_norm = Scalar(0);
    VectorX sample_radii;
    MatrixX M;
    const bool built = build_composite_distance_matrix(
        scalar_centers, quat_features, spec, options.kernel,
        X_norm, feature_norms, distance_norm, sample_radii, M);
    if (!built) {
        fr.status = FitStatus::INVALID_INPUT;
        fr.solver_path = SolverPath::FAILED;
        return fr;
    }

    // Slice 17A solve: ridge regression on the cols × cols normal equations.
    //   (MᵀM + λI) w_compact = Mᵀ Y
    // Note: fr.weights.rows() == cols (== spec.total_distance_columns(N)),
    // not N.  The scalar-only predict path treats weights as N × M; hetero
    // fit deliberately deviates here since Slice 17A does not yet ship a
    // predict path for the hetero branch.  The one-hot θ + predict_hetero
    // design arrives in Slice 17E.
    try {
        const Index cols = M.cols();
        MatrixX MtM = M.transpose() * M;
        for (Index i = 0; i < cols; ++i) {
            MtM(i, i) += lambda;
        }
        const MatrixX MtY = M.transpose() * targets;

        SolverPath path = SolverPath::FAILED;
        Scalar cond = Scalar(-1);
        const bool ok =
            solve_symmetric_system(MtM, MtY, fr.weights, path, cond);

        fr.solver_path = path;
        fr.condition_number = cond;
        fr.lambda_used = lambda;

        if (!ok) {
            fr.status = FitStatus::SINGULAR_MATRIX;
            return fr;
        }

        // Residual ||M w - Y||_F / ||Y||_F — the ridge-regression analog of
        // the scalar fit's "how well did the linear system close".  Used by
        // Group D InterpolationAtTrainingPoints to gate correctness.
        const MatrixX residual = M * fr.weights - targets;
        const Scalar y_norm = targets.norm();
        fr.residual_norm =
            (y_norm > Scalar(0)) ? (residual.norm() / y_norm) : residual.norm();

        // Populate 17A tail fields on success.
        // fr.feature_spec already set above; now fill the other 3.
        fr.quat_features = quat_features;  // owned copy
        fr.feature_norms = feature_norms;
        fr.distance_norm = distance_norm;

        // Store pre-normalisation scalar centers in fr.centers for audit
        // (legacy contract: fr.centers is the training input echo).  Slice
        // 17B may revisit this once cmt binary parity lands — see T-30.
        fr.centers = scalar_centers;

        // poly_coeffs: hetero path does not emit a polynomial tail in 17A.
        fr.poly_coeffs = MatrixX(0, targets.cols());

        fr.status = FitStatus::OK;
        return fr;
    } catch (...) {
        fr.status = FitStatus::INVALID_INPUT;
        fr.solver_path = SolverPath::FAILED;
        return fr;
    }
}

FitResult fit(const Eigen::Ref<const MatrixX>& scalar_centers,
              const std::vector<MatrixX>& quat_features,
              const Eigen::Ref<const MatrixX>& targets,
              const FitOptions& options,
              const FeatureSpec& spec,
              LambdaAuto auto_tag) noexcept {
    if (spec.is_scalar_only() && quat_features.empty()) {
        FitResult fr = fit(scalar_centers, targets, options, auto_tag);
        fr.feature_spec = spec;
        return fr;
    }
    // Hetero GCV on cols × cols normal equations is Slice 17E scope; 17A
    // falls back to the fixed-λ overload with the GCV-default 1e-6, mirroring
    // the legacy fallback value used when GCV scoring is pathological.
    return fit(scalar_centers, quat_features, targets, options, spec,
               Scalar(1e-6));
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
