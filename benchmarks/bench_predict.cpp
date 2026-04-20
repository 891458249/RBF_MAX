// =============================================================================
// benchmarks/bench_predict.cpp
// -----------------------------------------------------------------------------
// C-group benchmarks: predict hot path.  Covers dense (all-center traversal),
// kd-tree KNN acceleration, and the Slice 06 ScratchPool vs convenience-
// predict comparison (P6, deferred until Slice 09).
// =============================================================================
#include <benchmark/benchmark.h>

#include "common.hpp"
#include "rbfmax/interpolator.hpp"
#include "rbfmax/kernel_functions.hpp"
#include "rbfmax/solver.hpp"

using namespace rbfmax;

namespace {

/// Pre-trained fit + single query shared across cases.
struct PredictFixture {
    MatrixX centers;
    MatrixX targets;
    VectorX query;
    solver::FitResult fr;

    PredictFixture(Index n, Index d) {
        centers = rbfmax_bench::generate_uniform_cube(n, d);
        targets = rbfmax_bench::synthesize_targets(centers, 1);
        // Reproducible query sampled with a distinct seed so it's not a center.
        const MatrixX qm = rbfmax_bench::generate_uniform_cube(
            1, d, rbfmax_bench::kSeed + 1u);
        query = qm.row(0).transpose();
        solver::FitOptions opts(KernelParams(KernelType::kGaussian, 1.0));
        opts.poly_degree = -1;
        fr = solver::fit(centers, targets, opts, 1e-6);
    }
};

}  // namespace

// C1 — Dense predict at N=100.  Small enough that kdtree wouldn't engage
// anyway.
static void BM_Predict_Gaussian_N100(benchmark::State& state) {
    PredictFixture fx(100, 3);
    for (auto _ : state) {
        benchmark::DoNotOptimize(solver::predict(fx.fr, fx.query));
    }
}
BENCHMARK(BM_Predict_Gaussian_N100);

// C2 — Dense predict at N=1000 via solver::predict.  Each call creates a
// temporary ScratchPool internally (one heap alloc per call for the
// VectorX return + per-call pool), showing the baseline before the
// pool-reuse optimisation below.
static void BM_Predict_Gaussian_N1000_Dense(benchmark::State& state) {
    PredictFixture fx(1000, 3);
    for (auto _ : state) {
        benchmark::DoNotOptimize(solver::predict(fx.fr, fx.query));
    }
}
BENCHMARK(BM_Predict_Gaussian_N1000_Dense);

// C3 — RBFInterpolator KNN path at N=1000.  N ≥ 256 + Gaussian
// automatically engages kdtree.  Expected sub-μs per query in the design
// contract.
static void BM_Predict_Gaussian_N1000_KdTree(benchmark::State& state) {
    const auto centers = rbfmax_bench::generate_uniform_cube(1000, 3);
    const auto targets = rbfmax_bench::synthesize_targets(centers, 1);
    InterpolatorOptions opts(KernelParams(KernelType::kGaussian, 1.0));
    RBFInterpolator rbf(opts);
    rbf.fit(centers, targets);
    const MatrixX qm = rbfmax_bench::generate_uniform_cube(
        1, 3, rbfmax_bench::kSeed + 1u);
    const VectorX q = qm.row(0).transpose();
    for (auto _ : state) {
        benchmark::DoNotOptimize(rbf.predict(q));
    }
}
BENCHMARK(BM_Predict_Gaussian_N1000_KdTree);

// C4 pair — Slice 06 P6 validation: ScratchPool reuse vs convenience path.
// Both at N=500 dense so the comparison isolates allocation cost.

static void BM_Predict_ScratchPool(benchmark::State& state) {
    PredictFixture fx(500, 3);
    solver::ScratchPool pool(fx.fr.centers.cols(),
                             fx.fr.centers.rows(),
                             fx.fr.poly_coeffs.rows());
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            solver::predict_with_pool(fx.fr, fx.query, pool));
    }
}
BENCHMARK(BM_Predict_ScratchPool);

static void BM_Predict_Regular_N500(benchmark::State& state) {
    PredictFixture fx(500, 3);
    for (auto _ : state) {
        benchmark::DoNotOptimize(solver::predict(fx.fr, fx.query));
    }
}
BENCHMARK(BM_Predict_Regular_N500);

BENCHMARK_MAIN();
