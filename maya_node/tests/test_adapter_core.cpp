// =============================================================================
// maya_node/tests/test_adapter_core.cpp — Phase 2A Slice 10A
// -----------------------------------------------------------------------------
// 3 TEST blocks locking the HelloNode adapter arithmetic:
//   H1  hello_transform(0)  == 1.0 exactly  (Gaussian at r=0)
//   H2  hello_transform(1)  == exp(-1) to 1e-14
//   H3  hello_transform(-x) == hello_transform(x)  (even function)
//
// Random seed: 0xF5BFA9u (sequential after Slice 09's 0xF5BFA8u).
// Kept for future adapter helpers; H1-H3 are deterministic and do not
// use the RNG.
//
// Tolerance rationale (R-09 self-check):
//   * H1: exact equality — gaussian(r=0, eps=1) = exp(0) = 1.0,
//     bit-identical under IEEE 754.
//   * H2: 1e-14 absolute — machine epsilon ~2.22e-16, and the
//     arithmetic is a single std::exp call whose error is ≤ 1-2 ULP
//     (~5e-16).  1e-14 leaves ~45× safety margin.
//   * H3: exact equality — both sides compute std::abs first, the
//     resulting r is bit-identical, so the subsequent exp chain must
//     be as well.
// =============================================================================
#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#include "rbfmax/maya/adapter_core.hpp"

namespace {

using rbfmax::Scalar;
using rbfmax::maya::hello_transform;

// Reserved seed for future randomised adapter tests (Slice 11+).
// H1-H3 below are deterministic and do not consume it.  Keeping it as
// a [[maybe_unused]]-style marker attracts -Wunused on some toolchains;
// we instead reference it inside a dummy TEST to anchor it.
constexpr std::uint32_t kSeed = 0xF5BFA9u;

}  // namespace

TEST(HelloTransform, H1_ZeroIsUnity) {
    EXPECT_EQ(hello_transform(Scalar(0)), Scalar(1));
}

TEST(HelloTransform, H2_UnitInputIsExpMinusOne) {
    const Scalar got      = hello_transform(Scalar(1));
    const Scalar expected = std::exp(Scalar(-1));
    EXPECT_NEAR(got, expected, Scalar(1e-14));
}

TEST(HelloTransform, H3_EvenFunctionUnderSignFlip) {
    // Gaussian kernel at r = |x| is even in x.  Test across a small
    // sweep so a regression in std::abs would be caught.
    const Scalar samples[] = {Scalar(1), Scalar(0.5), Scalar(2.0),
                              Scalar(1e-3), Scalar(1e3)};
    for (Scalar x : samples) {
        EXPECT_EQ(hello_transform(x), hello_transform(-x))
            << "x = " << x;
    }
    // Anchor kSeed so -Wunused-variable does not fire under /WX.
    EXPECT_NE(kSeed, 0u);
}
