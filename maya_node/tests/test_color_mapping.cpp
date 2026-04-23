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

// =============================================================================
//  Slice 15 — G-group tests: grid sampling + prediction-field coloring
// =============================================================================
//
// Random seed reservation:
//   G-group / Slice 15: kSeedS15 = 0xF5BFAEu (no random fixtures yet,
//   reserved for future G9+ randomized tests).

using rbfmax::maya::build_grid_sample_points;
using rbfmax::maya::compute_grid_colors;

TEST(ColorMapping, G1_BuildGridSamples_D2_SmallGrid) {
    // G=3, extent=1.0 -> 9 rows x 2 cols, coords from -1 to +1 in 3 steps
    MatrixX out;
    build_grid_sample_points(3, 1.0, 0.0, 2, out);
    ASSERT_EQ(out.rows(), 9);
    ASSERT_EQ(out.cols(), 2);
    // Row layout (iy major):
    //   Row 0 = (-1, -1), Row 1 = (0, -1), Row 2 = (1, -1),
    //   Row 3 = (-1,  0), Row 4 = (0,  0), Row 5 = (1,  0),
    //   Row 6 = (-1,  1), Row 7 = (0,  1), Row 8 = (1,  1)
    EXPECT_DOUBLE_EQ(out(0, 0), -1.0);  EXPECT_DOUBLE_EQ(out(0, 1), -1.0);
    EXPECT_DOUBLE_EQ(out(4, 0),  0.0);  EXPECT_DOUBLE_EQ(out(4, 1),  0.0);
    EXPECT_DOUBLE_EQ(out(8, 0),  1.0);  EXPECT_DOUBLE_EQ(out(8, 1),  1.0);
}

TEST(ColorMapping, G2_BuildGridSamples_D3_ZFilled) {
    // G=2, extent=0.5, grid_z=1.5, D=3 -> 4 rows x 3 cols, z always 1.5
    MatrixX out;
    build_grid_sample_points(2, 0.5, 1.5, 3, out);
    ASSERT_EQ(out.rows(), 4);
    ASSERT_EQ(out.cols(), 3);
    for (Eigen::Index i = 0; i < 4; ++i) {
        EXPECT_DOUBLE_EQ(out(i, 2), 1.5) << "row " << i << " z";
    }
    EXPECT_DOUBLE_EQ(out(0, 0), -0.5);
    EXPECT_DOUBLE_EQ(out(0, 1), -0.5);
    EXPECT_DOUBLE_EQ(out(3, 0),  0.5);
    EXPECT_DOUBLE_EQ(out(3, 1),  0.5);
}

TEST(ColorMapping, G3_BuildGridSamples_D1_IgnoresYZ) {
    // D=1: only gx column; gy and grid_z discarded.
    // step = 2*2/(4-1) = 4/3
    // First 4 rows (iy=0): gx = -2, -2/3, 2/3, 2
    MatrixX out;
    build_grid_sample_points(4, 2.0, 100.0, 1, out);
    ASSERT_EQ(out.rows(), 16);
    ASSERT_EQ(out.cols(), 1);
    EXPECT_DOUBLE_EQ(out(0, 0), -2.0);
    EXPECT_NEAR(out(1, 0), -2.0 / 3.0, 1e-14);
    EXPECT_NEAR(out(2, 0),  2.0 / 3.0, 1e-14);
    EXPECT_DOUBLE_EQ(out(3, 0),  2.0);
}

TEST(ColorMapping, G4_BuildGridSamples_D5_ExtraZero) {
    // D=5: (gx, gy, grid_z, 0, 0)
    MatrixX out;
    build_grid_sample_points(2, 1.0, 0.5, 5, out);
    ASSERT_EQ(out.rows(), 4);
    ASSERT_EQ(out.cols(), 5);
    for (Eigen::Index i = 0; i < 4; ++i) {
        EXPECT_DOUBLE_EQ(out(i, 2), 0.5)  << "z col 2 row " << i;
        EXPECT_DOUBLE_EQ(out(i, 3), 0.0)  << "extra col 3 row " << i;
        EXPECT_DOUBLE_EQ(out(i, 4), 0.0)  << "extra col 4 row " << i;
    }
}

TEST(ColorMapping, G5_BuildGridSamples_InvalidG_EmptyOut) {
    MatrixX out;
    build_grid_sample_points(0, 1.0, 0.0, 2, out);
    EXPECT_EQ(out.rows(), 0) << "G=0";
    build_grid_sample_points(1, 1.0, 0.0, 2, out);
    EXPECT_EQ(out.rows(), 0) << "G=1";
    build_grid_sample_points(3, 0.0, 0.0, 2, out);
    EXPECT_EQ(out.rows(), 0) << "extent=0";
    build_grid_sample_points(3, -1.0, 0.0, 2, out);
    EXPECT_EQ(out.rows(), 0) << "extent<0";
    build_grid_sample_points(3, 1.0, 0.0, 0, out);
    EXPECT_EQ(out.rows(), 0) << "D=0";
}

TEST(ColorMapping, G6_ComputeGridColors_Simple) {
    // Strictly increasing L2 norms -> min->viridis(0), max->viridis(1)
    MatrixX P(4, 1);
    P << 0.1, 0.3, 0.7, 1.5;
    std::array<std::array<float, 4>, 4> out;
    compute_grid_colors(P, out.data(), 4);

    const auto c_min = map_scalar_to_color(Scalar(0));
    const auto c_max = map_scalar_to_color(Scalar(1));
    EXPECT_NEAR(out[0][0], c_min[0], kViridisTol);
    EXPECT_NEAR(out[0][1], c_min[1], kViridisTol);
    EXPECT_NEAR(out[0][2], c_min[2], kViridisTol);
    EXPECT_NEAR(out[3][0], c_max[0], kViridisTol);
    EXPECT_NEAR(out[3][1], c_max[1], kViridisTol);
    EXPECT_NEAR(out[3][2], c_max[2], kViridisTol);
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(out[i][3], 1.0f) << "alpha point " << i;
    }
}

TEST(ColorMapping, G7_ComputeGridColors_AllEqual) {
    MatrixX P(3, 2);
    P << 0.5, 0.5,
         0.5, 0.5,
         0.5, 0.5;
    std::array<std::array<float, 4>, 3> out;
    compute_grid_colors(P, out.data(), 3);

    const auto c0 = map_scalar_to_color(Scalar(0));
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            EXPECT_FLOAT_EQ(out[i][j], c0[j])
                << "point " << i << " channel " << j;
        }
    }
}

TEST(ColorMapping, G8_ComputeGridColors_NaNSafety) {
    MatrixX P(3, 1);
    P << 0.5, std::numeric_limits<double>::quiet_NaN(), 1.0;
    std::array<std::array<float, 4>, 3> out;
    compute_grid_colors(P, out.data(), 3);

    // NaN row -> defensive white
    EXPECT_FLOAT_EQ(out[1][0], 1.0f);
    EXPECT_FLOAT_EQ(out[1][1], 1.0f);
    EXPECT_FLOAT_EQ(out[1][2], 1.0f);
    EXPECT_FLOAT_EQ(out[1][3], 1.0f);

    // Finite rows must NOT be defensive white
    const bool row0_white =
        out[0][0] == 1.0f && out[0][1] == 1.0f && out[0][2] == 1.0f;
    const bool row2_white =
        out[2][0] == 1.0f && out[2][1] == 1.0f && out[2][2] == 1.0f;
    EXPECT_FALSE(row0_white) << "finite row 0 must not collapse to white";
    EXPECT_FALSE(row2_white) << "finite row 2 must not collapse to white";
}

}  // namespace
