// =============================================================================
// benchmarks/bench_solver.cpp
// -----------------------------------------------------------------------------
// B-group benchmarks: end-to-end solver::fit wall-clock for typical rig
// sample counts.  Covers fixed λ (LLT fast path) and GCV auto-λ (SVD) so the
// per-lambda-grid cost is visible.
// =============================================================================
#include <benchmark/benchmark.h>

#include "common.hpp"
#include "rbfmax/kernel_functions.hpp"
#include "rbfmax/solver.hpp"

using namespace rbfmax;

// B1 — Small rig: N=100, D=3, M=1, Gaussian, fixed λ.
static void BM_Fit_Gaussian_N100(benchmark::State& state) {
    const auto centers = rbfmax_bench::generate_uniform_cube(100, 3);
    const auto targets = rbfmax_bench::synthesize_targets(centers, 1);
    solver::FitOptions opts(KernelParams(KernelType::kGaussian, 1.0));
    opts.poly_degree = -1;
    for (auto _ : state) {
        auto fr = solver::fit(centers, targets, opts, 1e-6);
        benchmark::DoNotOptimize(fr);
    }
}
BENCHMARK(BM_Fit_Gaussian_N100)->Unit(benchmark::kMicrosecond);

// B2 — Medium rig: N=1000, fixed λ.  Kernel matrix 1000×1000 → LLT
// dominant cost.
static void BM_Fit_Gaussian_N1000(benchmark::State& state) {
    const auto centers = rbfmax_bench::generate_uniform_cube(1000, 3);
    const auto targets = rbfmax_bench::synthesize_targets(centers, 1);
    solver::FitOptions opts(KernelParams(KernelType::kGaussian, 1.0));
    opts.poly_degree = -1;
    for (auto _ : state) {
        auto fr = solver::fit(centers, targets, opts, 1e-6);
        benchmark::DoNotOptimize(fr);
    }
}
BENCHMARK(BM_Fit_Gaussian_N1000)->Unit(benchmark::kMillisecond);

// B3 — Medium rig with GCV auto-λ.  Dominant cost = one SVD of A plus the
// 50-point grid evaluation in closed form (no nested solves).
static void BM_Fit_Gaussian_N1000_GCV(benchmark::State& state) {
    const auto centers = rbfmax_bench::generate_uniform_cube(1000, 3);
    const auto targets = rbfmax_bench::synthesize_targets(centers, 1);
    solver::FitOptions opts(KernelParams(KernelType::kGaussian, 1.0));
    opts.poly_degree = -1;
    for (auto _ : state) {
        auto fr = solver::fit(centers, targets, opts, solver::kLambdaAuto);
        benchmark::DoNotOptimize(fr);
    }
}
BENCHMARK(BM_Fit_Gaussian_N1000_GCV)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
