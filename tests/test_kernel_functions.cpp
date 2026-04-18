// =============================================================================
// test_kernel_functions.cpp — Unit tests for rbfmax/kernel_functions.hpp
// -----------------------------------------------------------------------------
// Coverage targets (per the Phase 1 Gate criteria):
//   * Analytic values at canonical radii (0, 1, a few mid-range values).
//   * Limit correctness at r = 0 for TPS (no NaN leakage).
//   * Analytic derivatives agree with a centred finite-difference to ~1e-6.
//   * NaN input propagation for every kernel.
//   * Dispatcher parity with direct function calls.
//   * Round-trip of KernelType ↔ string.
//   * Minimum polynomial degree table.
// =============================================================================
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "rbfmax/kernel_functions.hpp"
#include "rbfmax/types.hpp"

namespace {

using rbfmax::KernelParams;
using rbfmax::KernelType;
using rbfmax::Scalar;

constexpr Scalar kTightTol = 1e-14;   // Direct arithmetic identities.
constexpr Scalar kFdTol    = 1e-6;    // Centred FD vs analytic derivative.
constexpr Scalar kRelaxTol = 1e-10;   // Single std::log/std::exp round-trip.

// Centred finite difference of a unary kernel functor at r.
template <typename F>
Scalar central_diff(F f, Scalar r, Scalar h = 1e-6) {
    return (f(r + h) - f(r - h)) / (static_cast<Scalar>(2.0) * h);
}

template <typename F>
Scalar central_diff_eps(F f, Scalar r, Scalar eps, Scalar h = 1e-6) {
    return (f(r + h, eps) - f(r - h, eps)) / (static_cast<Scalar>(2.0) * h);
}

}  // namespace

// =============================================================================
//  Value correctness
// =============================================================================

TEST(KernelValues, LinearMatchesIdentity) {
    EXPECT_DOUBLE_EQ(rbfmax::linear(0.0), 0.0);
    EXPECT_DOUBLE_EQ(rbfmax::linear(0.5), 0.5);
    EXPECT_DOUBLE_EQ(rbfmax::linear(1.0), 1.0);
    EXPECT_DOUBLE_EQ(rbfmax::linear(42.0), 42.0);
}

TEST(KernelValues, CubicAndQuinticMatchPowers) {
    const std::vector<Scalar> rs = {0.0, 0.1, 0.5, 1.0, 2.0, 7.3};
    for (Scalar r : rs) {
        EXPECT_NEAR(rbfmax::cubic(r),   r * r * r,             kTightTol);
        EXPECT_NEAR(rbfmax::quintic(r), r * r * r * r * r,     kTightTol);
    }
}

TEST(KernelValues, ThinPlateSplineReferencePoints) {
    // φ(1) = 1^2 · ln(1) = 0.
    EXPECT_DOUBLE_EQ(rbfmax::thin_plate_spline(1.0), 0.0);
    // φ(e) = e^2 · 1 = e^2.
    const Scalar e = std::exp(1.0);
    EXPECT_NEAR(rbfmax::thin_plate_spline(e), e * e, kRelaxTol);
    // φ(2) = 4 · ln(2).
    EXPECT_NEAR(rbfmax::thin_plate_spline(2.0), 4.0 * std::log(2.0), kRelaxTol);
}

TEST(KernelValues, ThinPlateSplineLimitAtZero) {
    // Explicit limit r→0+ of r^2·ln(r) is 0.  The clamp must return exactly 0,
    // not -inf or NaN, for any r ≤ kLogEps.
    EXPECT_DOUBLE_EQ(rbfmax::thin_plate_spline(0.0), 0.0);
    EXPECT_DOUBLE_EQ(rbfmax::thin_plate_spline(1e-50), 0.0);
    EXPECT_DOUBLE_EQ(rbfmax::thin_plate_spline_derivative(0.0), 0.0);
    EXPECT_DOUBLE_EQ(rbfmax::thin_plate_spline_derivative(1e-50), 0.0);
}

TEST(KernelValues, GaussianReferencePoints) {
    // φ(0) = 1 for any ε.
    EXPECT_DOUBLE_EQ(rbfmax::gaussian(0.0, 1.0),    1.0);
    EXPECT_DOUBLE_EQ(rbfmax::gaussian(0.0, 7.123),  1.0);
    // φ(1, 1) = 1/e.
    EXPECT_NEAR(rbfmax::gaussian(1.0, 1.0), std::exp(-1.0), kRelaxTol);
    // Symmetry in ε·r — same output for (r, ε) and (r/k, k·ε).
    EXPECT_NEAR(rbfmax::gaussian(2.0, 0.5),
                rbfmax::gaussian(1.0, 1.0), kRelaxTol);
}

TEST(KernelValues, InverseMultiquadricReferencePoints) {
    // φ(0) = 1 for any ε.
    EXPECT_DOUBLE_EQ(rbfmax::inverse_multiquadric(0.0, 1.0),   1.0);
    EXPECT_DOUBLE_EQ(rbfmax::inverse_multiquadric(0.0, 10.0),  1.0);
    // φ(1, 1) = 1 / sqrt(2).
    EXPECT_NEAR(rbfmax::inverse_multiquadric(1.0, 1.0),
                1.0 / std::sqrt(2.0), kRelaxTol);
    // Long-tail behaviour: φ decays like 1 / (ε r) for ε r ≫ 1.
    const Scalar big = 1e4;
    EXPECT_NEAR(rbfmax::inverse_multiquadric(big, 1.0),
                1.0 / big, 1e-8);
}

// =============================================================================
//  Derivative correctness  (analytic vs centred finite difference)
// =============================================================================

TEST(KernelDerivatives, LinearCubicQuinticMatchAnalytic) {
    const std::vector<Scalar> rs = {0.1, 0.5, 1.0, 2.0, 3.7};
    for (Scalar r : rs) {
        EXPECT_NEAR(rbfmax::linear_derivative(r),  1.0,       kTightTol);
        EXPECT_NEAR(rbfmax::cubic_derivative(r),   3.0 * r*r, kTightTol);
        EXPECT_NEAR(rbfmax::quintic_derivative(r), 5.0 * r*r*r*r, kTightTol);
    }
}

TEST(KernelDerivatives, ThinPlateSplineAgreesWithFD) {
    // Stay clear of the r=0 clamp so the FD stencil is fully inside the
    // smooth region.
    const std::vector<Scalar> rs = {0.25, 0.75, 1.5, 3.0};
    for (Scalar r : rs) {
        const Scalar analytic = rbfmax::thin_plate_spline_derivative(r);
        const Scalar fd       = central_diff(
            [](Scalar x) { return rbfmax::thin_plate_spline(x); }, r);
        EXPECT_NEAR(analytic, fd, kFdTol) << "r=" << r;
    }
}

TEST(KernelDerivatives, GaussianAgreesWithFD) {
    const std::vector<Scalar> rs  = {0.1, 0.5, 1.0, 2.0};
    const std::vector<Scalar> eps = {0.5, 1.0, 2.0};
    for (Scalar r : rs) {
        for (Scalar e : eps) {
            const Scalar analytic = rbfmax::gaussian_derivative(r, e);
            const Scalar fd       = central_diff_eps(
                [](Scalar x, Scalar ee) { return rbfmax::gaussian(x, ee); },
                r, e);
            EXPECT_NEAR(analytic, fd, kFdTol) << "r=" << r << " eps=" << e;
        }
    }
}

TEST(KernelDerivatives, InverseMultiquadricAgreesWithFD) {
    const std::vector<Scalar> rs  = {0.1, 0.5, 1.0, 2.0, 5.0};
    const std::vector<Scalar> eps = {0.5, 1.0, 2.0};
    for (Scalar r : rs) {
        for (Scalar e : eps) {
            const Scalar analytic = rbfmax::inverse_multiquadric_derivative(r, e);
            const Scalar fd       = central_diff_eps(
                [](Scalar x, Scalar ee) {
                    return rbfmax::inverse_multiquadric(x, ee);
                },
                r, e);
            EXPECT_NEAR(analytic, fd, kFdTol) << "r=" << r << " eps=" << e;
        }
    }
}

// =============================================================================
//  NaN propagation  (fail-fast; no silent masking)
// =============================================================================

TEST(KernelNaN, AllKernelsPropagateNaN) {
    const Scalar nan = std::numeric_limits<Scalar>::quiet_NaN();
    EXPECT_TRUE(std::isnan(rbfmax::linear(nan)));
    EXPECT_TRUE(std::isnan(rbfmax::cubic(nan)));
    EXPECT_TRUE(std::isnan(rbfmax::quintic(nan)));
    EXPECT_TRUE(std::isnan(rbfmax::thin_plate_spline(nan)));
    EXPECT_TRUE(std::isnan(rbfmax::gaussian(nan, 1.0)));
    EXPECT_TRUE(std::isnan(rbfmax::inverse_multiquadric(nan, 1.0)));

    EXPECT_TRUE(std::isnan(rbfmax::cubic_derivative(nan)));
    EXPECT_TRUE(std::isnan(rbfmax::thin_plate_spline_derivative(nan)));
    EXPECT_TRUE(std::isnan(rbfmax::gaussian_derivative(nan, 1.0)));
    EXPECT_TRUE(std::isnan(rbfmax::inverse_multiquadric_derivative(nan, 1.0)));
}

TEST(KernelInfinity, GaussianDecaysToZeroAtInfinity) {
    // A well-defined limit for a well-conditioned kernel: φ(+∞, ε>0) = 0.
    const Scalar inf = std::numeric_limits<Scalar>::infinity();
    EXPECT_DOUBLE_EQ(rbfmax::gaussian(inf, 1.0), 0.0);
    EXPECT_DOUBLE_EQ(rbfmax::inverse_multiquadric(inf, 1.0), 0.0);
}

// =============================================================================
//  Dispatcher parity
// =============================================================================

TEST(KernelDispatcher, ValueDispatchMatchesDirectCalls) {
    const Scalar r   = 1.37;
    const Scalar eps = 2.5;
    EXPECT_NEAR(rbfmax::evaluate_kernel(KernelType::kLinear,  r, eps),
                rbfmax::linear(r), kTightTol);
    EXPECT_NEAR(rbfmax::evaluate_kernel(KernelType::kCubic,   r, eps),
                rbfmax::cubic(r), kTightTol);
    EXPECT_NEAR(rbfmax::evaluate_kernel(KernelType::kQuintic, r, eps),
                rbfmax::quintic(r), kTightTol);
    EXPECT_NEAR(rbfmax::evaluate_kernel(KernelType::kThinPlateSpline, r, eps),
                rbfmax::thin_plate_spline(r), kTightTol);
    EXPECT_NEAR(rbfmax::evaluate_kernel(KernelType::kGaussian, r, eps),
                rbfmax::gaussian(r, eps), kTightTol);
    EXPECT_NEAR(rbfmax::evaluate_kernel(KernelType::kInverseMultiquadric, r, eps),
                rbfmax::inverse_multiquadric(r, eps), kTightTol);
}

TEST(KernelDispatcher, DerivativeDispatchMatchesDirectCalls) {
    const Scalar r   = 0.88;
    const Scalar eps = 1.5;
    EXPECT_NEAR(rbfmax::evaluate_kernel_derivative(KernelType::kLinear,  r, eps),
                rbfmax::linear_derivative(r), kTightTol);
    EXPECT_NEAR(rbfmax::evaluate_kernel_derivative(KernelType::kCubic,   r, eps),
                rbfmax::cubic_derivative(r), kTightTol);
    EXPECT_NEAR(rbfmax::evaluate_kernel_derivative(KernelType::kQuintic, r, eps),
                rbfmax::quintic_derivative(r), kTightTol);
    EXPECT_NEAR(rbfmax::evaluate_kernel_derivative(KernelType::kThinPlateSpline, r, eps),
                rbfmax::thin_plate_spline_derivative(r), kTightTol);
    EXPECT_NEAR(rbfmax::evaluate_kernel_derivative(KernelType::kGaussian, r, eps),
                rbfmax::gaussian_derivative(r, eps), kTightTol);
    EXPECT_NEAR(rbfmax::evaluate_kernel_derivative(KernelType::kInverseMultiquadric, r, eps),
                rbfmax::inverse_multiquadric_derivative(r, eps), kTightTol);
}

TEST(KernelDispatcher, ParamsOverloadForwardsCorrectly) {
    const KernelParams p{KernelType::kGaussian, 2.0};
    const Scalar r = 0.75;
    EXPECT_NEAR(rbfmax::evaluate_kernel(p, r),
                rbfmax::gaussian(r, p.eps), kTightTol);
    EXPECT_NEAR(rbfmax::evaluate_kernel_derivative(p, r),
                rbfmax::gaussian_derivative(r, p.eps), kTightTol);
}

// =============================================================================
//  String round-trip  (schema stability guard)
// =============================================================================

TEST(KernelTypeStrings, EveryEnumValueRoundTrips) {
    const KernelType all[] = {
        KernelType::kLinear,
        KernelType::kCubic,
        KernelType::kQuintic,
        KernelType::kThinPlateSpline,
        KernelType::kGaussian,
        KernelType::kInverseMultiquadric,
    };
    for (KernelType k : all) {
        const char* tag = rbfmax::kernel_type_to_string(k);
        ASSERT_NE(tag, nullptr);
        KernelType parsed = KernelType::kLinear;
        ASSERT_TRUE(rbfmax::kernel_type_from_string(tag, parsed)) << tag;
        EXPECT_EQ(static_cast<int>(parsed), static_cast<int>(k));
    }
}

TEST(KernelTypeStrings, UnknownTagIsRejected) {
    KernelType out = KernelType::kLinear;
    EXPECT_FALSE(rbfmax::kernel_type_from_string(nullptr, out));
    EXPECT_FALSE(rbfmax::kernel_type_from_string("",       out));
    EXPECT_FALSE(rbfmax::kernel_type_from_string("linear", out));  // Case-sensitive.
    EXPECT_FALSE(rbfmax::kernel_type_from_string("Wendland", out));
}

// =============================================================================
//  Metadata table
// =============================================================================

TEST(KernelMetadata, MinimumPolynomialDegreeTableMatchesDocument) {
    EXPECT_EQ(rbfmax::minimum_polynomial_degree(KernelType::kLinear),               0);
    EXPECT_EQ(rbfmax::minimum_polynomial_degree(KernelType::kCubic),                1);
    EXPECT_EQ(rbfmax::minimum_polynomial_degree(KernelType::kQuintic),              2);
    EXPECT_EQ(rbfmax::minimum_polynomial_degree(KernelType::kThinPlateSpline),      1);
    EXPECT_EQ(rbfmax::minimum_polynomial_degree(KernelType::kGaussian),            -1);
    EXPECT_EQ(rbfmax::minimum_polynomial_degree(KernelType::kInverseMultiquadric), -1);
}

TEST(KernelMetadata, RequiresShapeParameterFlag) {
    EXPECT_FALSE(rbfmax::requires_shape_parameter(KernelType::kLinear));
    EXPECT_FALSE(rbfmax::requires_shape_parameter(KernelType::kCubic));
    EXPECT_FALSE(rbfmax::requires_shape_parameter(KernelType::kQuintic));
    EXPECT_FALSE(rbfmax::requires_shape_parameter(KernelType::kThinPlateSpline));
    EXPECT_TRUE (rbfmax::requires_shape_parameter(KernelType::kGaussian));
    EXPECT_TRUE (rbfmax::requires_shape_parameter(KernelType::kInverseMultiquadric));
}
