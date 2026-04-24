// =============================================================================
// test_feature_spec.cpp — Phase 2A.5 Slice 17A unit tests
// -----------------------------------------------------------------------------
// 20 TEST blocks across 4 groups:
//   A — FeatureSpec / QuatBlock type contract                        (8 live)
//   B — 17A-SCALAR-ORACLE: byte-identical FitResult vs legacy fit()  (6 live)
//   C — quat-only mixed spaces (Full / SwingTwist)                   (3 SKIP)
//   D — hybrid scalar + quat                                         (3 SKIP)
//
// Groups C and D are the hetero pipeline tests; they land in Slice 17A
// step 3.4 with the full composite distance-matrix builder.  They are
// stubbed here with GTEST_SKIP so that the binary compiles in step 3.3
// but the 17A delivery is not mistaken for complete on Group A + B alone.
//
// Tolerance philosophy (Slice 17A)
// --------------------------------
// Group B is a bit-identity test — no floating-point tolerance is acceptable.
// std::memcmp over Eigen contiguous storage is the oracle for MatrixX / VectorX
// comparisons.  For scalar fields (lambda_used, condition_number, residual_norm,
// kernel.eps, distance_norm), the bit pattern is compared via std::memcpy into
// std::uint64_t, since C++20's std::bit_cast is not available on the Maya 2018
// GCC 4.8.2 C++11 floor.
// =============================================================================
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include <Eigen/Core>
#include <gtest/gtest.h>

#include "rbfmax/feature_spec.hpp"
#include "rbfmax/kernel_functions.hpp"
#include "rbfmax/solver.hpp"
#include "rbfmax/types.hpp"

namespace {

using rbfmax::FeatureSpec;
using rbfmax::Index;
using rbfmax::KernelParams;
using rbfmax::KernelType;
using rbfmax::MatrixX;
using rbfmax::QuatBlock;
using rbfmax::Scalar;
using rbfmax::SolverSpace;
using rbfmax::Vector3;
using rbfmax::VectorX;
using rbfmax::solver::FitOptions;
using rbfmax::solver::FitResult;
using rbfmax::solver::FitStatus;
using rbfmax::solver::kLambdaAuto;
using rbfmax::solver::SolverPath;

// ---------------------------------------------------------------------------
//  Bit-identity helpers
// ---------------------------------------------------------------------------

// C++11-compatible replacement for std::bit_cast (C++20).  Reads the object
// representation of a Scalar (double) into a 64-bit unsigned integer.
inline std::uint64_t scalar_bits(Scalar x) noexcept {
    static_assert(sizeof(Scalar) == sizeof(std::uint64_t),
                  "Scalar must be 64-bit for bit-identity tests");
    std::uint64_t u = 0;
    std::memcpy(&u, &x, sizeof(u));
    return u;
}

inline bool scalar_bit_equal(Scalar a, Scalar b) noexcept {
    return scalar_bits(a) == scalar_bits(b);
}

// Byte-level equality on Eigen contiguous storage.  Checks shape first, then
// memcmp over rows*cols*sizeof(Scalar).  Used on MatrixX / VectorX.
bool matrix_bytes_equal(const MatrixX& a, const MatrixX& b) {
    if (a.rows() != b.rows()) return false;
    if (a.cols() != b.cols()) return false;
    if (a.size() == 0) return true;  // 0×0 matrices are trivially equal
    const std::size_t nbytes =
        static_cast<std::size_t>(a.size()) * sizeof(Scalar);
    return std::memcmp(a.data(), b.data(), nbytes) == 0;
}

bool vector_bytes_equal(const VectorX& a, const VectorX& b) {
    if (a.size() != b.size()) return false;
    if (a.size() == 0) return true;
    const std::size_t nbytes =
        static_cast<std::size_t>(a.size()) * sizeof(Scalar);
    return std::memcmp(a.data(), b.data(), nbytes) == 0;
}

// Full 10-field byte-identical comparison of two FitResults on the pre-17A
// surface only.  The 4 new tail fields are verified separately — see
// expect_17a_tail_default_with_overlayed_spec.
void expect_fit_result_byte_identical(const FitResult& old_fr,
                                      const FitResult& new_fr) {
    // 1. weights — full bit-identity of N × M Eigen storage.
    EXPECT_TRUE(matrix_bytes_equal(old_fr.weights, new_fr.weights))
        << "weights byte-identity failed";

    // 2. poly_coeffs — may be 0×M; the helper handles that.
    EXPECT_TRUE(matrix_bytes_equal(old_fr.poly_coeffs, new_fr.poly_coeffs))
        << "poly_coeffs byte-identity failed";

    // 3. centers — N × D owned copy; must be identical.
    EXPECT_TRUE(matrix_bytes_equal(old_fr.centers, new_fr.centers))
        << "centers byte-identity failed";

    // 4. kernel.type — enum equality.
    EXPECT_EQ(static_cast<int>(old_fr.kernel.type),
              static_cast<int>(new_fr.kernel.type));

    // 5. kernel.eps — bit-identity via uint64 pattern, NOT EXPECT_DOUBLE_EQ
    //    (4-ULP tolerance is a violation of the SCALAR-ORACLE contract).
    EXPECT_TRUE(scalar_bit_equal(old_fr.kernel.eps, new_fr.kernel.eps))
        << "kernel.eps bit pattern drift: old=" << scalar_bits(old_fr.kernel.eps)
        << " new=" << scalar_bits(new_fr.kernel.eps);

    // 6. poly_degree.
    EXPECT_EQ(old_fr.poly_degree, new_fr.poly_degree);

    // 7. lambda_used — bit-identity.
    EXPECT_TRUE(scalar_bit_equal(old_fr.lambda_used, new_fr.lambda_used))
        << "lambda_used bit pattern drift";

    // 8. solver_path enum.
    EXPECT_EQ(static_cast<int>(old_fr.solver_path),
              static_cast<int>(new_fr.solver_path));

    // 9. status enum.
    EXPECT_EQ(static_cast<int>(old_fr.status),
              static_cast<int>(new_fr.status));

    // 10. condition_number — bit-identity.
    EXPECT_TRUE(scalar_bit_equal(old_fr.condition_number,
                                 new_fr.condition_number))
        << "condition_number bit pattern drift";

    // 11. residual_norm — bit-identity.
    EXPECT_TRUE(scalar_bit_equal(old_fr.residual_norm, new_fr.residual_norm))
        << "residual_norm bit pattern drift";
}

// Assertion over the 4 Slice 17A tail fields for a scalar-only dispatched
// FitResult.  feature_spec must reflect the caller's spec (NOT reset to
// default) — this is the explicit carrier of the heterogeneous API contract,
// even in the scalar-only branch where quat_features is empty.
void expect_17a_tail_default_with_overlayed_spec(
        const FitResult& fr, const FeatureSpec& expected_spec) {
    // feature_spec: caller's spec is echoed back (overlay in dispatch).
    EXPECT_EQ(fr.feature_spec.scalar_dim, expected_spec.scalar_dim);
    EXPECT_TRUE(fr.feature_spec.is_scalar_only());
    EXPECT_EQ(fr.feature_spec.quat_blocks.size(),
              expected_spec.quat_blocks.size());

    // quat_features: empty for scalar-only.
    EXPECT_TRUE(fr.quat_features.empty());

    // feature_norms: size 0.
    EXPECT_EQ(fr.feature_norms.size(), Index(0));

    // distance_norm: bit-identical to 0.0.
    EXPECT_TRUE(scalar_bit_equal(fr.distance_norm, Scalar(0)))
        << "distance_norm must be exactly 0.0 in scalar-only dispatch";
}

// ---------------------------------------------------------------------------
//  Small deterministic fixtures — no RNG so oracle comparisons stay stable.
// ---------------------------------------------------------------------------

MatrixX make_centers_2d_4pt() {
    MatrixX C(4, 2);
    C << 0.0, 0.0,
         1.0, 0.0,
         0.0, 1.0,
         1.0, 1.0;
    return C;
}

MatrixX make_targets_4x1_linear() {
    MatrixX Y(4, 1);
    Y << 0.0, 1.0, 1.0, 2.0;
    return Y;
}

MatrixX make_targets_4x3_multi() {
    MatrixX Y(4, 3);
    Y << 0.0,  0.5, -0.25,
         1.0,  0.5,  0.75,
         1.0, -0.5,  0.75,
         2.0, -0.5,  1.75;
    return Y;
}

}  // namespace

// ============================================================================
//  Group A — FeatureSpec / QuatBlock type contract
// ============================================================================

TEST(FeatureSpec, DefaultIsScalarOnly) {
    FeatureSpec spec;
    EXPECT_EQ(spec.scalar_dim, Index(0));
    EXPECT_TRUE(spec.quat_blocks.empty());
    EXPECT_TRUE(spec.is_scalar_only());
}

TEST(FeatureSpec, ScalarOnlyFromDimCtor) {
    FeatureSpec spec(Index(3));
    EXPECT_EQ(spec.scalar_dim, Index(3));
    EXPECT_TRUE(spec.is_scalar_only());
    EXPECT_EQ(spec.total_distance_columns(Index(5)), Index(5));  // N cols
}

TEST(FeatureSpec, TotalDistanceColumns_ScalarOnly) {
    // scalar_dim > 0 => N columns; scalar_dim == 0 => 0 columns.
    FeatureSpec with_scalars(Index(4));
    EXPECT_EQ(with_scalars.total_distance_columns(Index(7)), Index(7));

    FeatureSpec without;
    EXPECT_EQ(without.total_distance_columns(Index(7)), Index(0));
}

TEST(FeatureSpec, TotalDistanceColumns_MixedSpaces) {
    // Full → 1 col per pose; Swing/Twist/SwingTwist → 2 cols per pose.
    // Spec: scalar_dim=2, 3 quat blocks (Full, Swing, SwingTwist).
    // For N=5: 5 (scalar) + 1*5 (Full) + 2*5 (Swing) + 2*5 (SwingTwist) = 30.
    FeatureSpec spec(Index(2));
    spec.quat_blocks.push_back(QuatBlock(SolverSpace::Full, Vector3::Zero()));
    spec.quat_blocks.push_back(
        QuatBlock(SolverSpace::Swing, Vector3::UnitY()));
    spec.quat_blocks.push_back(
        QuatBlock(SolverSpace::SwingTwist, Vector3::UnitY()));

    EXPECT_EQ(spec.total_distance_columns(Index(5)), Index(5 + 5 + 10 + 10));
    EXPECT_FALSE(spec.is_scalar_only());
}

TEST(QuatBlock, DefaultAxisIsZero) {
    QuatBlock qb;
    EXPECT_EQ(qb.space, SolverSpace::Full);
    EXPECT_TRUE(qb.axis.isZero(Scalar(0)));  // exact zero
}

TEST(QuatBlock, SolverSpaceEnumRoundTrip) {
    // Verifies the enum backing type and the four canonical values.
    EXPECT_EQ(static_cast<int>(SolverSpace::Full), 0);
    EXPECT_EQ(static_cast<int>(SolverSpace::Swing), 1);
    EXPECT_EQ(static_cast<int>(SolverSpace::Twist), 2);
    EXPECT_EQ(static_cast<int>(SolverSpace::SwingTwist), 3);

    EXPECT_EQ(FeatureSpec::cols_per_pose(SolverSpace::Full), Index(1));
    EXPECT_EQ(FeatureSpec::cols_per_pose(SolverSpace::Swing), Index(2));
    EXPECT_EQ(FeatureSpec::cols_per_pose(SolverSpace::Twist), Index(2));
    EXPECT_EQ(FeatureSpec::cols_per_pose(SolverSpace::SwingTwist), Index(2));
}

TEST(FeatureSpec, NonEmptyQuatBlocksImpliesNonScalarOnly) {
    FeatureSpec spec(Index(0));
    spec.quat_blocks.push_back(QuatBlock(SolverSpace::Full, Vector3::Zero()));
    EXPECT_FALSE(spec.is_scalar_only());
}

TEST(FeatureSpec, MoveConstructible) {
    // std::vector<MatrixX> inside FitResult requires FeatureSpec to survive
    // container operations.  C++11 nothrow-moveable wins the "is_nothrow_move
    // _constructible" trait check when QuatBlock is trivially relocatable.
    FeatureSpec src(Index(4));
    src.quat_blocks.push_back(
        QuatBlock(SolverSpace::SwingTwist, Vector3::UnitZ()));

    FeatureSpec moved(std::move(src));
    EXPECT_EQ(moved.scalar_dim, Index(4));
    EXPECT_EQ(moved.quat_blocks.size(), std::size_t(1));
    EXPECT_EQ(moved.quat_blocks[0].space, SolverSpace::SwingTwist);
}

// ============================================================================
//  Group B — 17A-SCALAR-ORACLE (hard gate)
//
//  For each of 6 (kernel × λ × poly) combinations, fit both via legacy
//  solver::fit(C, Y, opts, λ) and the new heterogeneous solver::fit(C, {}, Y,
//  opts, FeatureSpec(C.cols()), λ).  Assert:
//    * all 10 pre-17A FitResult fields are byte-identical
//    * the 4 new tail fields are default-constructed except feature_spec,
//      which must reflect the caller's (scalar_dim=C.cols(), empty blocks).
// ============================================================================

TEST(Oracle17A, scalar_only_baseline_ByteIdentical_Gaussian) {
    const MatrixX C = make_centers_2d_4pt();
    const MatrixX Y = make_targets_4x1_linear();
    FitOptions opt(KernelParams(KernelType::kGaussian, 1.0));
    opt.poly_degree = -1;

    const FitResult old_fr =
        rbfmax::solver::fit(C, Y, opt, Scalar(1e-8));
    const FeatureSpec spec(C.cols());
    const FitResult new_fr = rbfmax::solver::fit(
        C, std::vector<MatrixX>{}, Y, opt, spec, Scalar(1e-8));

    expect_fit_result_byte_identical(old_fr, new_fr);
    expect_17a_tail_default_with_overlayed_spec(new_fr, spec);
    EXPECT_EQ(new_fr.status, FitStatus::OK);
}

TEST(Oracle17A, scalar_only_baseline_ByteIdentical_ThinPlateSpline) {
    const MatrixX C = make_centers_2d_4pt();
    const MatrixX Y = make_targets_4x3_multi();
    FitOptions opt(KernelParams(KernelType::kThinPlateSpline, 1.0));
    opt.poly_degree = 1;

    const FitResult old_fr =
        rbfmax::solver::fit(C, Y, opt, Scalar(1e-10));
    const FeatureSpec spec(C.cols());
    const FitResult new_fr = rbfmax::solver::fit(
        C, std::vector<MatrixX>{}, Y, opt, spec, Scalar(1e-10));

    expect_fit_result_byte_identical(old_fr, new_fr);
    expect_17a_tail_default_with_overlayed_spec(new_fr, spec);
}

TEST(Oracle17A, scalar_only_baseline_ByteIdentical_Cubic_WithPoly) {
    const MatrixX C = make_centers_2d_4pt();
    const MatrixX Y = make_targets_4x1_linear();
    FitOptions opt(KernelParams(KernelType::kCubic, 1.0));
    opt.poly_degree = 1;

    const FitResult old_fr =
        rbfmax::solver::fit(C, Y, opt, Scalar(1e-10));
    const FeatureSpec spec(C.cols());
    const FitResult new_fr = rbfmax::solver::fit(
        C, std::vector<MatrixX>{}, Y, opt, spec, Scalar(1e-10));

    expect_fit_result_byte_identical(old_fr, new_fr);
    expect_17a_tail_default_with_overlayed_spec(new_fr, spec);
    EXPECT_GT(new_fr.poly_coeffs.rows(), 0);  // poly tail actually present
}

TEST(Oracle17A, scalar_only_baseline_ByteIdentical_LambdaAuto) {
    const MatrixX C = make_centers_2d_4pt();
    const MatrixX Y = make_targets_4x1_linear();
    FitOptions opt(KernelParams(KernelType::kGaussian, 1.0));
    opt.poly_degree = -1;

    const FitResult old_fr = rbfmax::solver::fit(C, Y, opt, kLambdaAuto);
    const FeatureSpec spec(C.cols());
    const FitResult new_fr = rbfmax::solver::fit(
        C, std::vector<MatrixX>{}, Y, opt, spec, kLambdaAuto);

    expect_fit_result_byte_identical(old_fr, new_fr);
    expect_17a_tail_default_with_overlayed_spec(new_fr, spec);
    // GCV should have selected a positive λ.
    EXPECT_GT(new_fr.lambda_used, Scalar(0));
}

TEST(Oracle17A, scalar_only_baseline_PreservesFitStatus) {
    // Trigger INSUFFICIENT_SAMPLES via an empty-rows center matrix; the
    // dispatch must propagate legacy's validate_inputs status verbatim.
    const MatrixX C(0, 2);   // 0 samples
    const MatrixX Y(0, 1);
    FitOptions opt(KernelParams(KernelType::kGaussian, 1.0));

    const FitResult old_fr =
        rbfmax::solver::fit(C, Y, opt, Scalar(1e-8));
    const FeatureSpec spec(C.cols());
    const FitResult new_fr = rbfmax::solver::fit(
        C, std::vector<MatrixX>{}, Y, opt, spec, Scalar(1e-8));

    EXPECT_EQ(old_fr.status, FitStatus::INSUFFICIENT_SAMPLES);
    EXPECT_EQ(new_fr.status, FitStatus::INSUFFICIENT_SAMPLES);
    EXPECT_EQ(old_fr.solver_path, SolverPath::FAILED);
    EXPECT_EQ(new_fr.solver_path, SolverPath::FAILED);
    // Spec is still overlaid even on the INSUFFICIENT_SAMPLES path.
    expect_17a_tail_default_with_overlayed_spec(new_fr, spec);
}

TEST(Oracle17A, scalar_only_baseline_PreservesSolverPath) {
    // A well-conditioned small system normally takes the LLT path.  Confirm
    // dispatch preserves solver_path verbatim across a mix-output case.
    const MatrixX C = make_centers_2d_4pt();
    const MatrixX Y = make_targets_4x3_multi();
    FitOptions opt(KernelParams(KernelType::kGaussian, 1.0));
    opt.poly_degree = -1;

    const FitResult old_fr =
        rbfmax::solver::fit(C, Y, opt, Scalar(1e-6));
    const FeatureSpec spec(C.cols());
    const FitResult new_fr = rbfmax::solver::fit(
        C, std::vector<MatrixX>{}, Y, opt, spec, Scalar(1e-6));

    expect_fit_result_byte_identical(old_fr, new_fr);
    expect_17a_tail_default_with_overlayed_spec(new_fr, spec);
    // Weight shape sanity: N=4 centers × M=3 outputs.
    EXPECT_EQ(new_fr.weights.rows(), Index(4));
    EXPECT_EQ(new_fr.weights.cols(), Index(3));
}

// ---------------------------------------------------------------------------
//  Group C/D fixtures (Slice 17A step 3.4)
//
//  The two JSON files under tests/fixtures/ document these hand-computed
//  quaternion inputs verbatim; tests hard-code the doubles here for
//  numerical stability (avoids JSON parse rounding).  The fixtures serve as
//  audit trail and as the JSON payload 17B's cmt-binary-parity generator
//  will overwrite.
//
//  Quat storage convention in MatrixX: row = pose, columns = (x, y, z, w).
// ---------------------------------------------------------------------------

// Full-mode fixture: N=4, axis-angle closed-form unit quaternions.
//   pose 0 : identity         → (0, 0, 0, 1)
//   pose 1 : 90° around +Y    → (0, sin(π/4), 0, cos(π/4)) = (0, √2/2, 0, √2/2)
//   pose 2 : 180° around +Y   → (0, 1, 0, 0)
//   pose 3 : 45° around +Y    → (0, sin(π/8), 0, cos(π/8))
MatrixX make_quat_fixture_full_N4() {
    MatrixX Q(4, 4);
    const Scalar sqrt2_over_2 = Scalar(0.70710678118654757);  // √2/2
    const Scalar sin_pi_8    = Scalar(0.38268343236508978);   // sin(π/8)
    const Scalar cos_pi_8    = Scalar(0.92387953251128674);   // cos(π/8)
    Q << 0.0,           0.0,  0.0,            1.0,           // pose 0
         0.0,           sqrt2_over_2, 0.0,    sqrt2_over_2,  // pose 1
         0.0,           1.0,  0.0,            0.0,           // pose 2
         0.0,           sin_pi_8, 0.0,        cos_pi_8;      // pose 3
    return Q;
}

// SwingTwist-mode fixture (axis=Y): N=3, unit quaternions with non-trivial
// swing and twist components.
//   pose 0 : identity
//   pose 1 : 60° around +Y (pure twist around Y)
//   pose 2 : 30° around +X (pure swing away from Y axis)
MatrixX make_quat_fixture_swingtwist_N3() {
    MatrixX Q(3, 4);
    const Scalar s30 = Scalar(0.25881904510252074);  // sin(15°)
    const Scalar c30 = Scalar(0.96592582628906831);  // cos(15°)
    const Scalar s60 = Scalar(0.49999999999999994);  // sin(30°)
    const Scalar c60 = Scalar(0.86602540378443871);  // cos(30°)
    Q << 0.0, 0.0, 0.0, 1.0,   // identity
         0.0, s60, 0.0, c60,   // 60° about Y (twist only for axis=Y)
         s30, 0.0, 0.0, c30;   // 30° about X (swing-dominant for axis=Y)
    return Q;
}

// ============================================================================
//  Group C — quat-only mixed spaces (Slice 17A step 3.4)
// ============================================================================

TEST(HeteroFit, quat_only_FullMode_InterpolatesFixture) {
    // Heterogeneous fit with one Full-mode quat block (N=4, B=1, scalar_dim=0).
    // Expected: status OK, weights shape = (total_cols × M) = (4 × 1),
    // residual_norm small (ridge with λ=1e-8 on well-conditioned 4-pose data).
    const MatrixX scalar_centers(0, 0);
    std::vector<MatrixX> quat_features;
    quat_features.push_back(make_quat_fixture_full_N4());
    MatrixX Y(4, 1);
    Y << 0.1, 0.4, 0.9, 0.3;

    FeatureSpec spec;
    spec.quat_blocks.push_back(QuatBlock(SolverSpace::Full, Vector3::Zero()));

    FitOptions opt(KernelParams(KernelType::kGaussian, 1.0));
    opt.poly_degree = -1;

    // λ=1e-10: for ridge regression, residual_norm scales as O(λ/(σ_min+λ)),
    // so to hit the 1e-9 interpolation gate we need λ < 1e-9.  Matches the
    // hybrid Group D calibration.
    const FitResult fr = rbfmax::solver::fit(
        scalar_centers, quat_features, Y, opt, spec, Scalar(1e-10));

    ASSERT_EQ(fr.status, FitStatus::OK)
        << "hetero Full-mode fit must succeed on the 4-pose fixture";
    EXPECT_NE(fr.solver_path, SolverPath::FAILED);

    // Full-mode column count: 1 per pose × N = 4 cols total.
    EXPECT_EQ(fr.weights.rows(), Index(4))
        << "Full-mode weights.rows() == total_distance_columns(N) == N";
    EXPECT_EQ(fr.weights.cols(), Index(1));

    // Tail fields populated (distinguishes hetero branch from scalar-only).
    EXPECT_FALSE(fr.feature_spec.is_scalar_only());
    EXPECT_EQ(fr.quat_features.size(), std::size_t(1));
    EXPECT_EQ(fr.quat_features[0].rows(), Index(4));
    EXPECT_EQ(fr.quat_features[0].cols(), Index(4));

    // Interpolation property via ridge residual (tol 1e-9 per plan).
    EXPECT_LT(fr.residual_norm, Scalar(1e-9))
        << "ridge-regression residual too large; check kernel / lambda";
}

TEST(HeteroFit, quat_only_SwingTwistMode_CorrectColumnCount) {
    // Column count invariant: SwingTwist contributes 2N per block.
    // N=3, B=1 → total_cols = 2*3 = 6.
    const MatrixX scalar_centers(0, 0);
    std::vector<MatrixX> quat_features;
    quat_features.push_back(make_quat_fixture_swingtwist_N3());
    MatrixX Y(3, 1);
    Y << 1.0, -2.0, 3.0;

    FeatureSpec spec;
    spec.quat_blocks.push_back(
        QuatBlock(SolverSpace::SwingTwist, Vector3::UnitY()));

    // Helper's own arithmetic verified first — avoids surprise if the fit
    // pipeline silently renormalises total_distance_columns().
    EXPECT_EQ(spec.total_distance_columns(Index(3)), Index(6));

    FitOptions opt(KernelParams(KernelType::kGaussian, 1.0));
    const FitResult fr = rbfmax::solver::fit(
        scalar_centers, quat_features, Y, opt, spec, Scalar(1e-8));

    ASSERT_EQ(fr.status, FitStatus::OK);
    EXPECT_EQ(fr.weights.rows(), Index(6))
        << "SwingTwist weights.rows() == 2 * N";
    EXPECT_EQ(fr.weights.cols(), Index(1));
}

TEST(HeteroFit, quat_only_ValidationRejectsBlockMismatch) {
    // spec declares 2 blocks but caller supplies only 1 quat_features entry.
    const MatrixX scalar_centers(0, 0);
    std::vector<MatrixX> quat_features;
    quat_features.push_back(make_quat_fixture_full_N4());
    MatrixX Y(4, 1);
    Y << 0.1, 0.2, 0.3, 0.4;

    FeatureSpec spec;
    spec.quat_blocks.push_back(QuatBlock(SolverSpace::Full, Vector3::Zero()));
    spec.quat_blocks.push_back(
        QuatBlock(SolverSpace::SwingTwist, Vector3::UnitY()));  // extra block

    FitOptions opt(KernelParams(KernelType::kGaussian, 1.0));
    const FitResult fr = rbfmax::solver::fit(
        scalar_centers, quat_features, Y, opt, spec, Scalar(1e-8));

    EXPECT_EQ(fr.status, FitStatus::INVALID_INPUT)
        << "block count mismatch must surface INVALID_INPUT";
    EXPECT_EQ(fr.solver_path, SolverPath::FAILED);
}

// ============================================================================
//  Group D — hybrid scalar + quat (Slice 17A step 3.4)
// ============================================================================

TEST(HeteroFit, hybrid_scalar_quat_FitStatusOK_OnValidInput) {
    // N=3, scalar_dim=1, B=1 SwingTwist → cols = 3 + 2*3 = 9.
    MatrixX scalar_centers(3, 1);
    scalar_centers << -1.0, 0.0, 1.0;
    std::vector<MatrixX> quat_features;
    quat_features.push_back(make_quat_fixture_swingtwist_N3());
    MatrixX Y(3, 1);
    Y << 0.5, 1.5, 2.5;

    FeatureSpec spec(Index(1));
    spec.quat_blocks.push_back(
        QuatBlock(SolverSpace::SwingTwist, Vector3::UnitY()));

    FitOptions opt(KernelParams(KernelType::kGaussian, 1.0));
    const FitResult fr = rbfmax::solver::fit(
        scalar_centers, quat_features, Y, opt, spec, Scalar(1e-8));

    ASSERT_EQ(fr.status, FitStatus::OK);
    EXPECT_EQ(fr.weights.rows(), Index(9));
    EXPECT_EQ(fr.weights.cols(), Index(1));

    // Feature-norms captured for the single scalar column.
    EXPECT_EQ(fr.feature_norms.size(), Index(1));
    EXPECT_GT(fr.feature_norms(0), Scalar(0));
    // Distance norm (scalar-block Frobenius) populated non-zero.
    EXPECT_GT(fr.distance_norm, Scalar(0));
}

TEST(HeteroFit, hybrid_scalar_quat_InterpolationAtTrainingPoints) {
    // Same hybrid fit as above, residual gate at 1e-9.  For ridge regression
    // with very small λ this approximates the minimum-norm solution; on a
    // well-conditioned N=3 system the residual is dominated by λ, not
    // conditioning, so the tolerance is comfortable.
    MatrixX scalar_centers(3, 1);
    scalar_centers << -1.0, 0.0, 1.0;
    std::vector<MatrixX> quat_features;
    quat_features.push_back(make_quat_fixture_swingtwist_N3());
    MatrixX Y(3, 2);
    Y << 0.0,  1.0,
         0.5, -0.25,
         1.0,  0.75;

    FeatureSpec spec(Index(1));
    spec.quat_blocks.push_back(
        QuatBlock(SolverSpace::SwingTwist, Vector3::UnitY()));

    FitOptions opt(KernelParams(KernelType::kGaussian, 1.0));
    const FitResult fr = rbfmax::solver::fit(
        scalar_centers, quat_features, Y, opt, spec, Scalar(1e-10));

    ASSERT_EQ(fr.status, FitStatus::OK);
    EXPECT_LT(fr.residual_norm, Scalar(1e-9))
        << "hybrid ridge residual exceeds 1e-9 at training points";
}

// Compile-time verification that Slice 17A's FitResult tail additions have
// NOT broken the noexcept lifecycle of FitResult itself — the invariant that
// propagates into solver::predict's noexcept contract.  Constraint #6 from
// the Step 3.4 plan asks us to catch a silent regression where (e.g.) adding
// std::vector<MatrixX> causes FitResult default ctor / move / destructor to
// silently drop noexcept; the trait checks here are the tightest portable
// signal under C++11 (the expression-form noexcept(solver::predict(fr, x))
// depends on MSVC's Eigen::Ref construction being noexcept, which is not
// guaranteed across compiler / Eigen-version combinations and therefore
// unusable as a hard gate).  The predict functions themselves carry the
// noexcept keyword in solver.hpp and that remains the canonical guarantee.
TEST(HeteroFit, hybrid_scalar_quat_PredictNoexcept) {
    static_assert(
        std::is_nothrow_default_constructible<FitResult>::value,
        "FitResult default ctor must remain noexcept across Slice 17A tail fields");
    static_assert(
        std::is_nothrow_destructible<FitResult>::value,
        "FitResult destructor must remain noexcept across Slice 17A tail fields");
    static_assert(
        std::is_nothrow_default_constructible<FeatureSpec>::value,
        "FeatureSpec default ctor must be noexcept (17A contract)");
    static_assert(
        std::is_nothrow_destructible<FeatureSpec>::value,
        "FeatureSpec destructor must be noexcept");
    static_assert(
        std::is_nothrow_default_constructible<QuatBlock>::value,
        "QuatBlock default ctor must be noexcept");
    // The function-level noexcept keyword on solver::predict / predict_scalar
    // in solver.hpp remains the canonical guarantee; this TEST block validates
    // the FitResult-side preconditions that the keyword relies on.
    SUCCEED();
}
