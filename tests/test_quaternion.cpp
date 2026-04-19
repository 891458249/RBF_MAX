// =============================================================================
// test_quaternion.cpp — Unit tests for rbfmax/quaternion.hpp
// -----------------------------------------------------------------------------
// Coverage:
//   Swing-Twist
//     A1 PureTwistHasIdentitySwing
//     A2 PureSwingHasIdentityTwist
//     A3 ReconstructionHolds                       (1000 random samples)
//     A4 TwistAxisParallelToInput                  (1000 random samples)
//     A5 NinetyDegreeSwingDegeneratesToIdentityTwist
//     A6 DebugAssertOnNonUnitAxis                  (skipped — see body)
//
//   Log map
//     B1 IdentityMapsToZero
//     B2 RoundTripWithExp                          (1000, |v| < π)
//     B3 DoubleCoverShortestPath                   (sign-folded distance)
//     B4 NearIdentityTaylorBranch
//     B5 NearPiBoundary
//
//   Exp map
//     C1 ZeroMapsToIdentity
//     C2 PiRotationAroundXAxis
//     C3 RoundTripWithLog                          (1000 random samples)
//     C4 OutputIsUnitQuaternion                    (1000 random samples)
//     C5 NearZeroTaylorBranch
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <gtest/gtest.h>

#include "rbfmax/quaternion.hpp"
#include "rbfmax/types.hpp"

namespace {

using rbfmax::Quaternion;
using rbfmax::Scalar;
using rbfmax::Vector3;
using rbfmax::rotation::decompose_swing_twist;
using rbfmax::rotation::exp_map;
using rbfmax::rotation::log_map;
using rbfmax::rotation::SwingTwist;

constexpr std::uint32_t kSeed = 0xF5BFA2u;

// ---------------------------------------------------------------------------
//  Random helpers (Shoemake uniform unit quaternions, unit Vector3)
// ---------------------------------------------------------------------------

Quaternion make_unit_quaternion(std::mt19937& rng) {
    std::uniform_real_distribution<Scalar> u(Scalar(0), Scalar(1));
    const Scalar u1 = u(rng);
    const Scalar u2 = u(rng);
    const Scalar u3 = u(rng);
    const Scalar s1 = std::sqrt(Scalar(1) - u1);
    const Scalar s2 = std::sqrt(u1);
    const Scalar a  = Scalar(2) * rbfmax::kPi * u2;
    const Scalar b  = Scalar(2) * rbfmax::kPi * u3;
    return Quaternion(s2 * std::cos(b),  // w
                      s1 * std::sin(a),  // x
                      s1 * std::cos(a),  // y
                      s2 * std::sin(b)); // z
}

Vector3 make_unit_vector3(std::mt19937& rng) {
    std::uniform_real_distribution<Scalar> u(Scalar(-1), Scalar(1));
    for (;;) {
        const Scalar x = u(rng);
        const Scalar y = u(rng);
        const Scalar z = u(rng);
        const Scalar n2 = x * x + y * y + z * z;
        if (n2 > Scalar(1e-3) && n2 <= Scalar(1)) {
            const Scalar n = std::sqrt(n2);
            return Vector3(x / n, y / n, z / n);
        }
    }
}

// Random rotation vector with |v| ∈ (0, max_norm].  max_norm should be
// strictly < π so the round-trip log∘exp is well-defined.
Vector3 make_rotation_vector(std::mt19937& rng, Scalar max_norm) {
    std::uniform_real_distribution<Scalar> u(Scalar(0.001), max_norm);
    const Scalar mag = u(rng);
    return make_unit_vector3(rng) * mag;
}

// Quaternion difference treated modulo double cover: min over (q-p, q+p).
Scalar antipodal_distance(const Quaternion& a, const Quaternion& b) {
    const Scalar d_pos = (a.coeffs() - b.coeffs()).norm();
    const Scalar d_neg = (a.coeffs() + b.coeffs()).norm();
    return std::min(d_pos, d_neg);
}

}  // namespace

// =============================================================================
//  A — Swing-Twist
// =============================================================================

TEST(SwingTwistDecomposition, PureTwistHasIdentitySwing) {
    // q is a pure rotation about Z; decomposition with axis=Z should isolate
    // the entire rotation into the twist component.
    const Vector3 axis(0.0, 0.0, 1.0);
    const Scalar angle = 0.7;
    const Quaternion q(std::cos(angle / 2.0), 0.0, 0.0, std::sin(angle / 2.0));

    const SwingTwist st = decompose_swing_twist(q, axis);
    EXPECT_NEAR(st.swing.w(), 1.0, 1e-14);
    EXPECT_NEAR(st.swing.vec().norm(), 0.0, 1e-14);
    EXPECT_LT((st.twist.coeffs() - q.coeffs()).norm(), 1e-14);
}

TEST(SwingTwistDecomposition, PureSwingHasIdentityTwist) {
    // q is a pure rotation about X; with twist axis = Z, the entire
    // rotation should land in the swing (twist becomes Identity).
    const Vector3 twist_axis(0.0, 0.0, 1.0);
    const Scalar angle = 0.7;
    const Quaternion q(std::cos(angle / 2.0), std::sin(angle / 2.0), 0.0, 0.0);

    const SwingTwist st = decompose_swing_twist(q, twist_axis);
    EXPECT_NEAR(st.twist.w(), 1.0, 1e-14);
    EXPECT_NEAR(st.twist.vec().norm(), 0.0, 1e-14);
    EXPECT_LT((st.swing.coeffs() - q.coeffs()).norm(), 1e-14);
}

TEST(SwingTwistDecomposition, ReconstructionHolds) {
    // For 1000 random (q, axis), swing * twist must reconstruct q
    // (modulo double cover).
    std::mt19937 rng(kSeed);
    constexpr int kSamples = 1000;
    int failures = 0;
    for (int i = 0; i < kSamples; ++i) {
        const Quaternion q = make_unit_quaternion(rng);
        const Vector3 axis = make_unit_vector3(rng);
        const SwingTwist st = decompose_swing_twist(q, axis);
        const Quaternion recon = st.swing * st.twist;
        const Scalar dist = antipodal_distance(q, recon);
        if (dist > 1e-12) {
            ++failures;
        }
    }
    EXPECT_EQ(failures, 0);
}

TEST(SwingTwistDecomposition, TwistAxisParallelToInput) {
    // The twist's rotation axis (its imaginary direction) must be parallel
    // to the input axis whenever the twist is non-trivial.
    std::mt19937 rng(kSeed);
    constexpr int kSamples = 1000;
    int parallel_failures = 0;
    for (int i = 0; i < kSamples; ++i) {
        const Quaternion q = make_unit_quaternion(rng);
        const Vector3 axis = make_unit_vector3(rng);
        const SwingTwist st = decompose_swing_twist(q, axis);
        const Vector3 tw = st.twist.vec();
        if (tw.norm() > 1e-10) {
            const Vector3 tw_dir = tw.normalized();
            const Scalar dot = std::fabs(tw_dir.dot(axis));
            if (std::fabs(dot - 1.0) > 1e-10) {
                ++parallel_failures;
            }
        }
    }
    EXPECT_EQ(parallel_failures, 0);
}

TEST(SwingTwistDecomposition, NinetyDegreeSwingDegeneratesToIdentityTwist) {
    // A 180° rotation about an axis perpendicular to the chosen twist axis
    // sends the twist axis to its opposite; the twist is unobservable, and
    // the convention is twist = Identity, swing = q.
    const Vector3 twist_axis(0.0, 0.0, 1.0);
    const Vector3 perp(1.0, 0.0, 0.0);
    const Quaternion q(0.0, perp.x(), perp.y(), perp.z());  // 180° about X

    const SwingTwist st = decompose_swing_twist(q, twist_axis);
    EXPECT_NEAR(st.twist.w(), 1.0, 1e-14);
    EXPECT_NEAR(st.twist.vec().norm(), 0.0, 1e-14);
    EXPECT_LT((st.swing.coeffs() - q.coeffs()).norm(), 1e-14);
}

TEST(SwingTwistDecomposition, DebugAssertOnNonUnitAxis) {
    // The unit-axis contract is enforced via EIGEN_ASSERT only in Debug
    // builds, and EIGEN_ASSERT's behavior (abort vs trap) is platform/
    // build-config dependent.  Cross-platform GTest death-test semantics
    // are too fragile to assert against, so this test documents the
    // contract and skips verification.  See quaternion.hpp doc block.
    GTEST_SKIP() << "Contract enforced via EIGEN_ASSERT in Debug; "
                    "death-test semantics differ across MSVC and GCC.";
}

// =============================================================================
//  B — Log map
// =============================================================================

TEST(LogMap, IdentityMapsToZero) {
    const Vector3 r = log_map(Quaternion::Identity());
    EXPECT_NEAR(r.norm(), 0.0, 1e-14);
}

TEST(LogMap, RoundTripWithExp) {
    // For 1000 random rotation vectors with |v| < π, log(exp(v)) should
    // recover v to within 1e-10.
    std::mt19937 rng(kSeed);
    constexpr int kSamples = 1000;
    constexpr Scalar kMaxNorm = static_cast<Scalar>(3.14);  // safely below π
    int failures = 0;
    for (int i = 0; i < kSamples; ++i) {
        const Vector3 v_orig = make_rotation_vector(rng, kMaxNorm);
        const Quaternion q = exp_map(v_orig);
        const Vector3 v_back = log_map(q);
        if ((v_back - v_orig).norm() > 1e-10) {
            ++failures;
        }
    }
    EXPECT_EQ(failures, 0);
}

TEST(LogMap, DoubleCoverShortestPath) {
    // log(q) and log(-q) describe the same rotation; under the shortest-
    // path convention they must agree, modulo a sign flip at the w==0
    // boundary (where the choice is intrinsically ambiguous).  Use the
    // sign-folded distance min(|a-b|, |a+b|) to absorb that ambiguity.
    std::mt19937 rng(kSeed);
    constexpr int kSamples = 200;
    int failures = 0;
    for (int i = 0; i < kSamples; ++i) {
        const Quaternion q = make_unit_quaternion(rng);
        const Quaternion neg(-q.w(), -q.x(), -q.y(), -q.z());
        const Vector3 a = log_map(q);
        const Vector3 b = log_map(neg);
        const Scalar dist = std::min((a - b).norm(), (a + b).norm());
        if (dist > 1e-12) {
            ++failures;
        }
    }
    EXPECT_EQ(failures, 0);
}

TEST(LogMap, NearIdentityTaylorBranch) {
    // |v| ~ 1e-10 hits the Taylor branch (threshold 1e-8). Round-trip
    // precision should match double machine epsilon.
    const Vector3 v_in(1e-10, 0.0, 0.0);
    const Quaternion q = exp_map(v_in);
    const Vector3 v_out = log_map(q);
    EXPECT_NEAR(v_out.x(), v_in.x(), 1e-14);
    EXPECT_NEAR(v_out.y(), 0.0, 1e-14);
    EXPECT_NEAR(v_out.z(), 0.0, 1e-14);
}

TEST(LogMap, NearPiBoundary) {
    // |v| = π - 1e-6 stresses asin near saturation; precision floor 1e-8.
    const Scalar theta = rbfmax::kPi - static_cast<Scalar>(1e-6);
    const Vector3 v_in(theta, 0.0, 0.0);
    const Quaternion q = exp_map(v_in);
    const Vector3 v_out = log_map(q);
    EXPECT_NEAR(v_out.x(), theta, 1e-8);
    EXPECT_NEAR(v_out.y(), 0.0, 1e-8);
    EXPECT_NEAR(v_out.z(), 0.0, 1e-8);
}

// =============================================================================
//  C — Exp map
// =============================================================================

TEST(ExpMap, ZeroMapsToIdentity) {
    const Quaternion q = exp_map(Vector3::Zero());
    EXPECT_NEAR(q.w(), 1.0, 1e-14);
    EXPECT_NEAR(q.vec().norm(), 0.0, 1e-14);
}

TEST(ExpMap, PiRotationAroundXAxis) {
    const Vector3 r(rbfmax::kPi, 0.0, 0.0);
    const Quaternion q = exp_map(r);
    // Expected: (cos(π/2), sin(π/2), 0, 0) = (0, 1, 0, 0).
    EXPECT_NEAR(q.w(), 0.0, 1e-14);
    EXPECT_NEAR(q.x(), 1.0, 1e-14);
    EXPECT_NEAR(q.y(), 0.0, 1e-14);
    EXPECT_NEAR(q.z(), 0.0, 1e-14);
}

TEST(ExpMap, RoundTripWithLog) {
    // For 1000 random unit q (folded to w >= 0), exp(log(q)) recovers q.
    std::mt19937 rng(kSeed);
    constexpr int kSamples = 1000;
    int failures = 0;
    for (int i = 0; i < kSamples; ++i) {
        Quaternion q = make_unit_quaternion(rng);
        if (q.w() < Scalar(0)) {
            q = Quaternion(-q.w(), -q.x(), -q.y(), -q.z());
        }
        const Vector3 r = log_map(q);
        const Quaternion q_back = exp_map(r);
        if ((q_back.coeffs() - q.coeffs()).norm() > 1e-10) {
            ++failures;
        }
    }
    EXPECT_EQ(failures, 0);
}

TEST(ExpMap, OutputIsUnitQuaternion) {
    // exp of any rotation vector must produce a unit quaternion; bounded
    // to |v| ≤ 4π so sin/cos remain in their well-conditioned regime.
    std::mt19937 rng(kSeed);
    constexpr int kSamples = 1000;
    int failures = 0;
    for (int i = 0; i < kSamples; ++i) {
        const Vector3 v = make_rotation_vector(
            rng, static_cast<Scalar>(4.0 * 3.14159265358979));
        const Quaternion q = exp_map(v);
        if (std::fabs(q.norm() - 1.0) > 1e-14) {
            ++failures;
        }
    }
    EXPECT_EQ(failures, 0);
}

TEST(ExpMap, NearZeroTaylorBranch) {
    // |v| ~ 1e-10 hits the Taylor branch in exp_map.
    const Vector3 v(1e-10, 0.0, 0.0);
    const Quaternion q = exp_map(v);
    // Expected: (cos(5e-11), sin(5e-11), 0, 0) ≈ (1, 5e-11, 0, 0).
    EXPECT_NEAR(q.w(), 1.0, 1e-14);
    EXPECT_NEAR(q.x(), 5e-11, 1e-14);
    EXPECT_NEAR(q.y(), 0.0, 1e-14);
    EXPECT_NEAR(q.z(), 0.0, 1e-14);
}
