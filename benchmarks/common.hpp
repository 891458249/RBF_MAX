// =============================================================================
// benchmarks/common.hpp
// -----------------------------------------------------------------------------
// Shared helpers for bench_*.cpp in the Slice 09 Google Benchmark suite.
// Deterministic synthetic data generators keyed on a fixed seed so repeated
// runs produce comparable numbers.
// =============================================================================
#ifndef RBFMAX_BENCH_COMMON_HPP
#define RBFMAX_BENCH_COMMON_HPP

#include <cmath>
#include <random>

#include "rbfmax/types.hpp"

namespace rbfmax_bench {

/// Fixed benchmark random seed.  Sequential after Slice 08's 0xF5BFA7u.
constexpr unsigned kSeed = 0xF5BFA8u;

/// Uniformly sample N points in the [-1, 1]^D hyper-cube.
inline rbfmax::MatrixX generate_uniform_cube(rbfmax::Index n,
                                              rbfmax::Index d,
                                              unsigned seed = kSeed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<rbfmax::Scalar> dist(-1.0, 1.0);
    rbfmax::MatrixX out(n, d);
    for (rbfmax::Index i = 0; i < n; ++i) {
        for (rbfmax::Index j = 0; j < d; ++j) {
            out(i, j) = dist(rng);
        }
    }
    return out;
}

/// Synthesize targets y_ij = sin(x_i0) + cos(x_i1) [+ j * 0.1].  When D=1
/// the cos term is omitted.  Per-column phase offset keeps multi-output
/// columns linearly independent so M > 1 benchmarks exercise real matrix
/// arithmetic.
inline rbfmax::MatrixX synthesize_targets(const rbfmax::MatrixX& centers,
                                           rbfmax::Index m = 1) {
    rbfmax::MatrixX y(centers.rows(), m);
    for (rbfmax::Index i = 0; i < centers.rows(); ++i) {
        const rbfmax::Scalar base =
            std::sin(centers(i, 0)) +
            (centers.cols() >= 2 ? std::cos(centers(i, 1))
                                 : rbfmax::Scalar{0});
        for (rbfmax::Index j = 0; j < m; ++j) {
            y(i, j) = base + static_cast<rbfmax::Scalar>(j) * 0.1;
        }
    }
    return y;
}

}  // namespace rbfmax_bench

#endif  // RBFMAX_BENCH_COMMON_HPP
