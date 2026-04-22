// =============================================================================
// maya_node/tests/test_color_mapping.cpp — Phase 2B Slice 14 F-group
// -----------------------------------------------------------------------------
// Random seed reservation (per Phase 2 convention):
//   F-group / Slice 14: kSeedS14 = 0xF5BFADu (no random fixtures yet,
//   reserved for future F9+ randomized tests).
//
// Tolerance:
//   1e-2 absolute per channel against matplotlib's 256-entry viridis LUT
//   sampled at v=0.0/0.5/1.0.  This is the contract the polynomial
//   approximation in color_mapping.cpp targets; tightening would force
//   a LUT or higher-order fit (out of scope for HM-1).
// =============================================================================
#include <gtest/gtest.h>

#include <rbfmax/maya/color_mapping.hpp>

#include <Eigen/Core>

#include <array>
#include <cmath>

namespace {

using rbfmax::maya::map_scalar_to_color;
using rbfmax::maya::compute_center_colors;
using rbfmax::MatrixX;
using rbfmax::Scalar;

// matplotlib viridis reference at three sample points:
//   v=0.0  → (0.267, 0.005, 0.329)
//   v=0.5  → (0.127, 0.567, 0.551)
//   v=1.0  → (0.993, 0.906, 0.144)
constexpr float kViridisTol = 1e-2f;

// -----------------------------------------------------------------------------
//  F1-F3 — viridis polynomial sampled at the three canonical points
// -----------------------------------------------------------------------------

TEST(ColorMapping, F1_MapScalarToColor_Zero) {
    const auto c = map_scalar_to_color(Scalar(0));
    EXPECT_NEAR(c[0], 0.267f, kViridisTol);
    EXPECT_NEAR(c[1], 0.005f, kViridisTol);
    EXPECT_NEAR(c[2], 0.329f, kViridisTol);
    EXPECT_FLOAT_EQ(c[3], 1.0f);
}

TEST(ColorMapping, F2_MapScalarToColor_Half) {
    const auto c = map_scalar_to_color(Scalar(0.5));
    EXPECT_NEAR(c[0], 0.127f, kViridisTol);
    EXPECT_NEAR(c[1], 0.567f, kViridisTol);
    EXPECT_NEAR(c[2], 0.551f, kViridisTol);
}

TEST(ColorMapping, F3_MapScalarToColor_One) {
    const auto c = map_scalar_to_color(Scalar(1));
    EXPECT_NEAR(c[0], 0.993f, kViridisTol);
    EXPECT_NEAR(c[1], 0.906f, kViridisTol);
    EXPECT_NEAR(c[2], 0.144f, kViridisTol);
}

// -----------------------------------------------------------------------------
//  F4-F5 — clamping below 0 and above 1
// -----------------------------------------------------------------------------

TEST(ColorMapping, F4_MapScalarToColor_ClampBelow) {
    const auto c_below = map_scalar_to_color(Scalar(-0.5));
    const auto c_zero  = map_scalar_to_color(Scalar(0));
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(c_below[i], c_zero[i]) << "channel " << i;
    }
}

TEST(ColorMapping, F5_MapScalarToColor_ClampAbove) {
    const auto c_above = map_scalar_to_color(Scalar(1.5));
    const auto c_one   = map_scalar_to_color(Scalar(1));
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(c_above[i], c_one[i]) << "channel " << i;
    }
}

// -----------------------------------------------------------------------------
//  F6 — alpha is always exactly 1, even for out-of-range inputs
// -----------------------------------------------------------------------------

TEST(ColorMapping, F6_MapScalarToColor_AlphaAlwaysOne) {
    for (Scalar v : {Scalar(0), Scalar(0.25), Scalar(0.5),
                     Scalar(0.75), Scalar(1), Scalar(-1), Scalar(2)}) {
        EXPECT_FLOAT_EQ(map_scalar_to_color(v)[3], 1.0f)
            << "alpha drift at v=" << v;
    }
}

// -----------------------------------------------------------------------------
//  F7 — compute_center_colors: simple ascending norms maps min→start max→end
// -----------------------------------------------------------------------------

TEST(ColorMapping, F7_ComputeCenterColors_Simple) {
    MatrixX W(4, 1);
    W << 0.1, 0.3, 0.7, 1.5;
    std::array<std::array<float, 4>, 4> out;
    compute_center_colors(W, out.data(), 4);

    const auto c_min = map_scalar_to_color(Scalar(0));
    EXPECT_NEAR(out[0][0], c_min[0], kViridisTol);
    EXPECT_NEAR(out[0][1], c_min[1], kViridisTol);
    EXPECT_NEAR(out[0][2], c_min[2], kViridisTol);

    const auto c_max = map_scalar_to_color(Scalar(1));
    EXPECT_NEAR(out[3][0], c_max[0], kViridisTol);
    EXPECT_NEAR(out[3][1], c_max[1], kViridisTol);
    EXPECT_NEAR(out[3][2], c_max[2], kViridisTol);

    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(out[i][3], 1.0f) << "alpha at center " << i;
    }
}

// -----------------------------------------------------------------------------
//  F8 — degenerate: all weights identical → all colors equal & = viridis(0)
// -----------------------------------------------------------------------------

TEST(ColorMapping, F8_ComputeCenterColors_AllEqual) {
    MatrixX W(3, 2);
    W << 0.5, 0.5,
         0.5, 0.5,
         0.5, 0.5;
    std::array<std::array<float, 4>, 3> out;
    compute_center_colors(W, out.data(), 3);

    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(out[0][i], out[1][i])
            << "channel " << i << " differs row 0 vs row 1";
        EXPECT_FLOAT_EQ(out[1][i], out[2][i])
            << "channel " << i << " differs row 1 vs row 2";
    }

    const auto c0 = map_scalar_to_color(Scalar(0));
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(out[0][i], c0[i])
            << "degenerate fallback channel " << i;
    }
}

}  // namespace
