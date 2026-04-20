// =============================================================================
// benchmarks/bench_kernel.cpp
// -----------------------------------------------------------------------------
// A-group benchmarks: raw throughput of individual kernel primitives + the
// evaluate_kernel dispatch overhead.  These numbers put a ceiling on per-
// sample fit/predict work and help decide whether future SIMD work would pay
// off.
// =============================================================================
#include <benchmark/benchmark.h>

#include "rbfmax/kernel_functions.hpp"

using namespace rbfmax;

// A1 — Gaussian raw throughput.  exp(-(ε r)²) is one exp plus two muls.
static void BM_Kernel_Gaussian(benchmark::State& state) {
    const Scalar eps = 1.0;
    Scalar r = 0.5;
    for (auto _ : state) {
        benchmark::DoNotOptimize(gaussian(r, eps));
        r += 1e-9;  // Prevents LICM from hoisting r as a constant.
    }
}
BENCHMARK(BM_Kernel_Gaussian);

// A2 — Thin Plate Spline: r² log r with r ≤ kLogEps branch guard.
static void BM_Kernel_ThinPlateSpline(benchmark::State& state) {
    Scalar r = 0.5;
    for (auto _ : state) {
        benchmark::DoNotOptimize(thin_plate_spline(r));
        r += 1e-9;
    }
}
BENCHMARK(BM_Kernel_ThinPlateSpline);

// A3 — Dispatch overhead: evaluate_kernel(params, r) is a small switch that
// selects Gaussian here.  Difference vs A1 is the switch cost alone.
static void BM_Kernel_Dispatch(benchmark::State& state) {
    KernelParams p(KernelType::kGaussian, 1.0);
    Scalar r = 0.5;
    for (auto _ : state) {
        benchmark::DoNotOptimize(evaluate_kernel(p, r));
        r += 1e-9;
    }
}
BENCHMARK(BM_Kernel_Dispatch);

BENCHMARK_MAIN();
