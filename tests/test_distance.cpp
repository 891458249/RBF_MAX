// =============================================================================
// test_distance.cpp — Unit tests for rbfmax/distance.hpp
// -----------------------------------------------------------------------------
// Coverage:
//   Euclidean
//     * Basic values on Vector3 and VectorX
//     * Interop with Eigen::Map (zero-copy view)
//     * Self distance is exactly zero
//     * NaN propagation
//
//   Quaternion geodesic
//     * Identity           : d(q, q)          = 0
//     * Antipodal          : d(q, −q)         = 0   (double cover)
//     * 90° rotation       : d(id, q_90)      = π/2 (well-conditioned, acos)
//     * 180° rotation      : d(id, q_180)     = π   (|dot| = 0, acos stable)
//     * Near-identity      : asin fallback stays linear for θ ≈ 1e-7
//     * Symmetry           : d(a, b) == d(b, a)
//     * Triangle inequality: 1000 samples, fixed seed, no flakiness
// =============================================================================
#include <cmath>
#include <limits>
#include <random>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <gtest/gtest.h>

#include "rbfmax/distance.hpp"
#include "rbfmax/types.hpp"

namespace {

using rbfmax::Quaternion;
using rbfmax::Scalar;
using rbfmax::Vector3;
using rbfmax::VectorX;
using rbfmax::metric::distance;
using rbfmax::metric::quaternion_abs_dot;
using rbfmax::metric::quaternion_geodesic_distance;
using rbfmax::metric::squared_distance;

constexpr Scalar kTightTol = 1e-14;
constexpr Scalar kQuatTol  = 1e-13;  // acos branch.
constexpr Scalar kAsinTol  = 1e-12;  // asin fallback branch.

// Fixed-seed generator — any flaky run is a real numerical regression,
// never an RNG collision.  Arbitrary hex chosen once and frozen.
constexpr std::uint32_t kSeed = 0xF5BFA1u;

Quaternion make_unit_quat(Scalar w, Scalar x, Scalar y, Scalar z) {
    Quaternion q(w, x, y, z);
    q.normalize();
    return q;
}

}  // namespace

// =============================================================================
//  Euclidean
// =============================================================================

TEST(EuclideanDistance, BasicValuesVector3) {
    Vector3 a(0.0, 0.0, 0.0);
    Vector3 b(3.0, 4.0, 0.0);
    EXPECT_DOUBLE_EQ(squared_distance(a, b), 25.0);
    EXPECT_DOUBLE_EQ(distance(a, b),          5.0);
}

TEST(EuclideanDistance, DynamicVectorMatchesFixed) {
    Vector3 a(0.0, 0.0, 0.0);
    Vector3 b(3.0, 4.0, 0.0);
    VectorX ax = a;
    VectorX bx = b;
    EXPECT_DOUBLE_EQ(squared_distance(ax, bx), 25.0);
    EXPECT_DOUBLE_EQ(distance(ax, bx),          5.0);
    // Cross-shape: Vector3 vs VectorX — template resolves at call site.
    EXPECT_DOUBLE_EQ(squared_distance(a, bx), 25.0);
    EXPECT_DOUBLE_EQ(distance(ax, b),          5.0);
}

TEST(EuclideanDistance, MapZeroCopyInterop) {
    // External raw buffer (simulates Maya attribute data block).
    double buf_a[3] = {1.0, 2.0, 3.0};
    double buf_b[3] = {4.0, 6.0, 3.0};
    Eigen::Map<const Vector3> ma(buf_a);
    Eigen::Map<const Vector3> mb(buf_b);
    EXPECT_DOUBLE_EQ(squared_distance(ma, mb), 9.0 + 16.0 + 0.0);
    EXPECT_DOUBLE_EQ(distance(ma, mb),         5.0);
}

TEST(EuclideanDistance, SelfDistanceIsExactlyZero) {
    Vector3 a(1.5, -2.3, 0.7);
    EXPECT_DOUBLE_EQ(squared_distance(a, a), 0.0);
    EXPECT_DOUBLE_EQ(distance(a, a),         0.0);
}

TEST(EuclideanDistance, NaNPropagation) {
    const Scalar nan = std::numeric_limits<Scalar>::quiet_NaN();
    Vector3 a(0.0, 0.0, 0.0);
    Vector3 b(1.0, nan, 0.0);
    EXPECT_TRUE(std::isnan(squared_distance(a, b)));
    EXPECT_TRUE(std::isnan(distance(a, b)));
}

// =============================================================================
//  Quaternion — canonical corner cases
// =============================================================================

TEST(QuaternionGeodesic, IdentityYieldsZero) {
    const Quaternion q = Quaternion::Identity();
    EXPECT_DOUBLE_EQ(quaternion_geodesic_distance(q, q), 0.0);
    EXPECT_DOUBLE_EQ(quaternion_abs_dot(q, q),           1.0);
}

TEST(QuaternionGeodesic, AntipodalYieldsZero_DoubleCover) {
    // q and −q represent the same rotation; distance must be 0, not π.
    const Quaternion q   = make_unit_quat(0.5, 0.5, 0.5, 0.5);   // 120° about (1,1,1).
    const Quaternion nq  = Quaternion(-q.w(), -q.x(), -q.y(), -q.z());
    EXPECT_NEAR(quaternion_geodesic_distance(q, nq), 0.0, kAsinTol);
    EXPECT_NEAR(quaternion_abs_dot(q, nq),           1.0, kTightTol);
}

TEST(QuaternionGeodesic, HalfWayRotationYieldsHalfPi) {
    // 90° rotation about X:  q = (cos 45°, sin 45°, 0, 0).
    const Scalar     c45 = std::cos(rbfmax::kPi / 4.0);
    const Scalar     s45 = std::sin(rbfmax::kPi / 4.0);
    const Quaternion id  = Quaternion::Identity();
    const Quaternion q90 = make_unit_quat(c45, s45, 0.0, 0.0);
    EXPECT_NEAR(quaternion_geodesic_distance(id, q90),
                rbfmax::kHalfPi, kQuatTol);
}

TEST(QuaternionGeodesic, PhysicallyOppositeRotationYieldsPi) {
    // 180° about X:  q = (0, 1, 0, 0).  |dot(id, q)| = 0, acos(0) = π/2,
    // multiplied by 2 gives π.  This is the well-conditioned region for
    // acos (not the asin fallback).
    const Quaternion id   = Quaternion::Identity();
    const Quaternion q180 = make_unit_quat(0.0, 1.0, 0.0, 0.0);
    EXPECT_NEAR(quaternion_geodesic_distance(id, q180),
                rbfmax::kPi, kQuatTol);
}

TEST(QuaternionGeodesic, NearIdentityLinearInAngle) {
    // Construct q2 = rotation by a very small angle θ about an arbitrary axis.
    // Distance should equal θ to ~1e-12 thanks to the asin half-angle branch.
    const Scalar theta = 1e-7;
    const Quaternion id = Quaternion::Identity();
    const Quaternion q_small = make_unit_quat(
        std::cos(theta / 2.0),
        std::sin(theta / 2.0) * 0.6,
        std::sin(theta / 2.0) * 0.8,
        0.0);
    const Scalar d = quaternion_geodesic_distance(id, q_small);
    EXPECT_NEAR(d, theta, 1e-12);  // asin fallback precision floor ~1e-7 relative.
}

TEST(QuaternionGeodesic, SymmetryHolds) {
    const Quaternion a = make_unit_quat( 0.3,  0.4, -0.1,  0.85);
    const Quaternion b = make_unit_quat(-0.2,  0.6,  0.7,  0.3);
    const Scalar dab = quaternion_geodesic_distance(a, b);
    const Scalar dba = quaternion_geodesic_distance(b, a);
    EXPECT_DOUBLE_EQ(dab, dba);
}

TEST(QuaternionGeodesic, DegenerateEqualityReducesToIdentity) {
    // d(a, b) must equal d(a, c) when b == c (a shape sanity check, not
    // a deep metric property).
    const Quaternion a = make_unit_quat( 0.1,  0.2,  0.3, 0.9);
    const Quaternion b = make_unit_quat(-0.3,  0.1, -0.4, 0.8);
    const Quaternion c = b;
    EXPECT_NEAR(quaternion_geodesic_distance(a, b),
                quaternion_geodesic_distance(a, c), kTightTol);
}

// =============================================================================
//  Quaternion — statistical validation (triangle inequality)
// =============================================================================

TEST(QuaternionGeodesic, TriangleInequality_1000Samples_FixedSeed) {
    // 1000 random unit-quaternion triples.  Triangle inequality on SO(3) is
    // strict (d(a,c) ≤ d(a,b) + d(b,c)); we allow a tiny 1e-12 slack for
    // the accumulated error across three acos/asin evaluations.
    std::mt19937                         rng(kSeed);
    std::uniform_real_distribution<Scalar> u(-1.0, 1.0);

    auto random_unit_quat = [&]() {
        // Marsaglia-style rejection inside the unit 4-ball, then normalise.
        for (;;) {
            const Scalar x = u(rng);
            const Scalar y = u(rng);
            const Scalar z = u(rng);
            const Scalar w = u(rng);
            const Scalar n2 = x * x + y * y + z * z + w * w;
            if (n2 > 1e-3 && n2 <= 1.0) {
                const Scalar n = std::sqrt(n2);
                return Quaternion(w / n, x / n, y / n, z / n);
            }
        }
    };

    constexpr int kSamples = 1000;
    constexpr Scalar kSlack = 1e-12;
    int violations = 0;
    for (int i = 0; i < kSamples; ++i) {
        const Quaternion a = random_unit_quat();
        const Quaternion b = random_unit_quat();
        const Quaternion c = random_unit_quat();

        const Scalar dab = quaternion_geodesic_distance(a, b);
        const Scalar dbc = quaternion_geodesic_distance(b, c);
        const Scalar dac = quaternion_geodesic_distance(a, c);

        // Must be in [0, π] for all pairs.
        EXPECT_GE(dab, 0.0);
        EXPECT_LE(dab, rbfmax::kPi + kSlack);

        if (dac > dab + dbc + kSlack) {
            ++violations;
        }
    }
    EXPECT_EQ(violations, 0)
        << "Triangle inequality violated in " << violations
        << " / " << kSamples << " samples.";
}

TEST(QuaternionGeodesic, RangeAlwaysInClosedZeroPi) {
    // Independently verify the [0, π] range guarantee — sanity check so that
    // a future refactor cannot silently produce negative or >π outputs.
    std::mt19937 rng(kSeed);
    std::uniform_real_distribution<Scalar> u(-1.0, 1.0);
    auto random_unit_quat = [&]() {
        for (;;) {
            const Scalar x = u(rng), y = u(rng), z = u(rng), w = u(rng);
            const Scalar n2 = x * x + y * y + z * z + w * w;
            if (n2 > 1e-3 && n2 <= 1.0) {
                const Scalar n = std::sqrt(n2);
                return Quaternion(w / n, x / n, y / n, z / n);
            }
        }
    };
    for (int i = 0; i < 500; ++i) {
        const Quaternion a = random_unit_quat();
        const Quaternion b = random_unit_quat();
        const Scalar d = quaternion_geodesic_distance(a, b);
        EXPECT_GE(d, 0.0)               << "i=" << i;
        EXPECT_LE(d, rbfmax::kPi + 1e-12) << "i=" << i;
    }
}

TEST(QuaternionAbsDot, AntipodalReduction) {
    const Quaternion q  = make_unit_quat(0.1, 0.2, 0.3, 0.9);
    const Quaternion nq = Quaternion(-q.w(), -q.x(), -q.y(), -q.z());
    // |dot(q, q)| == |dot(q, −q)| == 1 under antipodal reduction.
    EXPECT_NEAR(quaternion_abs_dot(q,  q),  1.0, kTightTol);
    EXPECT_NEAR(quaternion_abs_dot(q, nq),  1.0, kTightTol);
}
