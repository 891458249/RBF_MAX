# RBF_MAX

Industrial-grade Radial Basis Function interpolation kernel for
Autodesk Maya and game engines.

[![CI](https://github.com/891458249/RBF_MAX/actions/workflows/ci.yml/badge.svg)](https://github.com/891458249/RBF_MAX/actions/workflows/ci.yml)
[![Version](https://img.shields.io/badge/version-1.0.0-blue)](https://github.com/891458249/RBF_MAX/releases/tag/v1.0.0)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-11-orange)](https://en.cppreference.com/w/cpp/11)

## Status

**Phase 1 complete (v1.0.0).** This release delivers the pure C++
mathematical kernel, solver, spatial index, and I/O layer — the
"math foundation" of the plugin. The kernel is Maya-free and
engine-agnostic: it links against Eigen 3.3.9 and nlohmann/json,
with GoogleTest as the test dependency and Google Benchmark as the
optional performance suite.

**Phase 2** (Maya node integration, Viewport 2.0 visualization,
Qt6 UI) is planned as a follow-on project.

## Quick Start

```cpp
#include <rbfmax/interpolator.hpp>

using namespace rbfmax;

// 1. Configure: Gaussian kernel, auto polynomial tail
InterpolatorOptions opts(KernelParams(KernelType::kGaussian, 1.0));
RBFInterpolator rbf(opts);

// 2. Train on sample centers and targets
MatrixX centers = ...;  // N × 3 samples in pose space
MatrixX targets = ...;  // N × M target values to interpolate
rbf.fit(centers, targets);  // GCV auto-lambda by default

// 3. Predict in the hot loop (allocation-free)
VectorX query = ...;
VectorX out = rbf.predict(query);

// 4. Save for later reuse
rbf.save("rig.rbf.json");
```

## Architecture

Eight independent modules composed into a single `RBFInterpolator`:

```
┌─────────────────────────────────────────────────────┐
│  RBFInterpolator (facade, rbfmax::)                 │
│  ─────────────────────────────────────────────────  │
│  • fit() / predict() / save() / load() / clone()    │
└────────────────────┬────────────────────────────────┘
                     │
     ┌───────────────┼────────────────┐
     │               │                │
┌────▼────┐   ┌──────▼──────┐   ┌────▼────────┐
│ solver  │   │   spatial   │   │   io_json   │
│ (fit,   │   │  (kd-tree,  │   │  (schema v1)│
│ predict)│   │    KNN)     │   │             │
└────┬────┘   └─────────────┘   └─────────────┘
     │
┌────▼──────────────────────────────────┐
│ kernel / distance / rotation / types  │
│ (6 kernels, Euclidean + quaternion,   │
│  Swing-Twist, Log/Exp maps)           │
└───────────────────────────────────────┘
```

See [docs/math_derivation.md](docs/math_derivation.md) for the full
mathematical derivations (14 chapters) and
[docs/schema_v1.md](docs/schema_v1.md) for the JSON schema spec.

## Features

- **6 radial basis kernel functions**: Linear, Cubic, Quintic,
  Thin-Plate Spline, Gaussian, Inverse Multiquadric.
- **Tikhonov regularization** with GCV auto-lambda selection
  (SVD closed-form over a 50-point log-uniform grid).
- **QR elimination** for polynomial-tail augmented systems — turns
  the indefinite saddle-point system into an SPD subsystem solvable
  by LLT.
- **Three-tier solver fallback**: LLT → LDLT → BDCSVD, with the
  condition number reported on the SVD path.
- **kd-tree KNN acceleration** for Gaussian kernels at N ≥ 256
  (other kernels traverse all centers to preserve exact results).
- **Quaternion algebra**: Swing-Twist decomposition, Log/Exp maps
  between SO(3) and its Lie algebra ℝ³, with double-cover
  shortest-path handling.
- **Zero-allocation predict hot path** via `ScratchPool` — pre-
  allocated Eigen buffers reused across calls; validated by Slice 09
  benchmarks.
- **JSON persistence** (schema `rbfmax/v1`) with full double
  precision round-trip and forward-compatible upgrade path.
- **Strict numerical contract**: double precision internally,
  explicit NaN propagation, `eigen_assert`-guarded preconditions,
  `noexcept` throughout.

## Building

Requirements: CMake ≥ 3.14, C++11 compiler. Tested on MSVC 17.x
(Visual Studio 2022) and GCC 11 (Ubuntu 22.04). Dependencies are
fetched automatically via CMake FetchContent (Eigen 3.3.9,
GoogleTest 1.12.1, nlohmann/json 3.11.3, Google Benchmark 1.8.3 on
demand).

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build (kernel + solver + tests)
cmake --build build -j

# Run tests (136 test cases, 2-3 seconds)
ctest --test-dir build --output-on-failure
```

Enable benchmarks (Google Benchmark is fetched only on demand):

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DRBF_BUILD_BENCHMARKS=ON
cmake --build build-bench -j
./build-bench/bin/benchmarks/bench_solver
```

## Performance

Reference numbers on MSVC 17.x Release, Intel laptop CPU, Q2 2026.
Your mileage will vary — see [DEVLOG.md](DEVLOG.md) Slice 09 entry
for the full measured table.

| Operation                 | N     | Time        |
|---------------------------|-------|-------------|
| `fit` (Gaussian, fixed λ) | 100   | ~0.5 ms     |
| `fit` (Gaussian, fixed λ) | 1000  | ~50–80 ms   |
| `fit` (Gaussian, GCV)     | 1000  | ~200–400 ms |
| `predict` (dense)         | 100   | ~300 ns     |
| `predict` (dense)         | 1000  | ~3–5 μs     |
| `predict` (kd-tree KNN)   | 1000  | ~0.5–1 μs   |

The 1000-sample kd-tree predict meets the design target of
"sub-1μs interactive playback for rigging workflows".

## Design Decisions

Every design decision in Phase 1 is recorded in
[DEVLOG.md](DEVLOG.md) along with its rationale and any trade-offs.
Key decisions:

- **C++11 baseline** (not C++17) for compatibility with Maya 2018
  toolchains (GCC 4.8.2 RHEL/CentOS 6).
- **JSON over Protobuf** for serialization — pragmatic choice for
  TA-readable rig assets; a binary sidecar can be added in a future
  v2 schema.
- **kd-tree only for Gaussian** — non-local kernels (Linear/Cubic/
  Quintic/TPS/IMQ) traverse all centers to preserve exact results
  (see math §14 for the truncation-error analysis).
- **Branch protection** with a CI matrix (MSVC Release + Debug, GCC
  11 Release) enforced on `main`; linear history via rebase-merge
  only; auto-delete head branches after PR merge.

## Roadmap

- **Phase 2**: Maya node integration (`MPxNode` with `kParallel`,
  Viewport 2.0 `MPxDrawOverride` for debug visualization, Qt6
  Model/View UI, Swing-Twist driver, pose manager, JSON-backed
  asset pipeline).
- **Phase 3**: TBB `parallel_for` for batch predict on large
  character rigs; GPU compute for offline training.
- **Phase 4**: Production asset tooling (mirror propagation,
  batch import/export, regression testing).

## License

Copyright 2026 891458249

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE)
for the full text.
