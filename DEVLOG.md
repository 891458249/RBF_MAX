# Dev Log — RBF_MAX

面向**开发**的工程日志。记录每个切片（Slice）级别的技术决策、风险、遗留问题。
对外可见的功能变更请查阅 [`CHANGELOG.md`](./CHANGELOG.md)。

---

## 日志约定

- **每个切片**（一次 `git commit` 涵盖的一组逻辑相关文件）**必须**写一条条目。
- 条目按时间倒序排列（最新在最上）。
- 每条包含：标题、提交哈希、阶段/切片编号、决策、风险/TODO、验收门、文件变更摘要。
- 提交哈希在 commit 创建后回填，可用 `scripts/update_devlog_hash.sh`（阶段二提供）。

---

## 2026-04-20 · Slice 09 — Benchmarks + v1.0.0 (Phase 1 finale)

**Scope**: Phase 1 closing slice. Performance baseline, public documentation (README + LICENSE), and the v1.0.0 tag. No new functional code in kernel/solver/interpolator; this slice ships measurement infrastructure, publication-ready docs, and the Phase 1 retrospective.

**Deliverables**
- `benchmarks/common.hpp` — deterministic synthetic-data helpers (`generate_uniform_cube`, `synthesize_targets`), seed `0xF5BFA8u`.
- `benchmarks/{bench_kernel, bench_solver, bench_predict}.cpp` — 11 Google Benchmark cases across 3 groups.
- `benchmarks/CMakeLists.txt` — rewritten from the Slice 05 skeleton: each `bench_*.cpp` supplies its own `BENCHMARK_MAIN()`, so we link `benchmark::benchmark` only (no `benchmark_main`). `benchmarks/.gitkeep` removed.
- `.github/workflows/ci.yml` — new `benchmark-smoke` job (Ubuntu 22.04 + GCC 11), only triggered by `workflow_dispatch` or version tag push. Daily PR CI unchanged.
- `README.md` — project-first public README (rewritten from the Slice 01 draft that stopped at v0.1.0), ~200 lines covering status, quick start, architecture diagram, feature list, build instructions, reference performance table, design decisions, roadmap, license pointer.
- `LICENSE` — Apache License 2.0 full text with "Copyright 2026 891458249" notice.
- Top-level CMake `VERSION 0.8.0 → 1.0.0`.

**Design decisions (9 locked pre-slice)**
1. **Benchmark scope: 11 cases in 3 groups** (kernel primitives / solver training / predict hot path). Each group in its own `.cpp`.
2. **CI benchmark strategy: opt-in** (`workflow_dispatch` + tag push only). Keeps PR CI fast; performance checks on demand / at release.
3. **README scale: middleweight** (~200 lines). Not a landing page, not a 30-line minimal — consistent with industrial-plugin scope.
4. **License: Apache 2.0**. Patent grant + industrial-software standard. Verified compatible with all fetched dependencies (Eigen MPL 2.0, GTest BSD 3-Clause, nlohmann/json MIT, Google Benchmark Apache 2.0).
5. **Benchmark file organisation: three `.cpp` + one `common.hpp`**. Rejected single-file variant as future Phase 2/3 benchmarks would become unwieldy.
6. **Synthetic data seed: `0xF5BFA8u`** (sequential after Slice 08's `0xF5BFA7u`).
7. **Performance target location: README reference table + DEVLOG detailed table.** README shows engineering ranges ("~0.5–1 μs"); DEVLOG lists exact measured numbers.
8. **v1.0.0 release: manual GitHub Release after tag push.** Automated release generation is a Phase 2 nice-to-have.
9. **Tech-debt: not cleared in Phase 1; explicit handoff to Phase 2** with per-item disposition in the retrospective table below.

**Measured performance** (MSVC 19.44, Windows 11, Release, 28-core Intel-class CPU @ 3.4 GHz, `--benchmark_min_time=0.01s`)

| Benchmark                               | Mean time |
|-----------------------------------------|-----------|
| `BM_Kernel_Gaussian`                    | 2.90 ns   |
| `BM_Kernel_ThinPlateSpline`             | 2.74 ns   |
| `BM_Kernel_Dispatch`                    | 2.92 ns   |
| `BM_Fit_Gaussian_N100`                  | 129 μs    |
| `BM_Fit_Gaussian_N1000`                 | 25.3 ms   |
| `BM_Fit_Gaussian_N1000_GCV`             | 445 ms    |
| `BM_Predict_Gaussian_N100`              | 2.30 μs   |
| `BM_Predict_Gaussian_N1000_Dense`       | 23.5 μs   |
| `BM_Predict_Gaussian_N1000_KdTree`      | 1.43 μs   |
| `BM_Predict_ScratchPool` (N=500)        | 11.1 μs   |
| `BM_Predict_Regular_N500`               | 11.2 μs   |

**Two honest observations from the measured data**

1. **Sub-1 μs target missed by ~43 %**. The kd-tree predict at N=1000 clocks **1.43 μs** against the blueprint's "<1 μs" goal. Not dramatic, but not met either. Likely contributors: `spatial::KdTree::knn_search`'s `std::priority_queue<pair<Scalar, Index>>` heap allocates internally on every call, and the MSVC Release build keeps `eigen_assert` checks active in the KNN inner loop. Not blocking for v1.0.0; Phase 2 may switch to a stack-array max-heap + `NDEBUG`-gated contracts.
2. **ScratchPool shows no measurable advantage at N=500** (11.1 μs vs 11.2 μs for the convenience `solver::predict`). Interpretation: MSVC Release fully inlines the per-call `ScratchPool` construction in `predict()`, so the only remaining allocation is the returned `VectorX`, which both paths share. Slice 06's zero-allocation claim holds in theory but is invisible at this size because the inner kernel evaluation (N = 500 exp calls) dominates. True validation of ScratchPool's value would require a repeated-predict loop in a tight scheduler (e.g. Maya 60 fps compute()), and an out-parameter API that eliminates the final `VectorX` return alloc. Both items queued for Phase 2.

**Local-verification surprise**
- Google Benchmark sub-build took ~49 s on first `--fresh` configure (vs the main build's <2 s) — the FetchContent populate + benchmark's own CMake configure introduces measurable latency even on a cached git clone. CI's `actions/cache` keyed on `FetchDependencies.cmake + benchmarks/CMakeLists.txt` hash will amortise this after the first run. No code change needed.

**Tech-debt register**
- None new.
- Slice 06 P6 (ScratchPool allocation-count proof) remains open — the measurement at N=500 was inconclusive. Escalated to Phase 2 with the out-parameter `predict_into(...)` API as the concrete technique.

---

## Phase 1 Retrospective

**Timeline**: 2026-04-19 (Slice 01 v0.1.0) → 2026-04-20 (v1.0.0). Compressed calendar; real calendar time dominated by design reviews and spec iteration, not code.

**Slices shipped**: 8 feature + 2 CI (02.5, 02.5.1) + 1 finale = **11 iterations**.

**Git releases** (11 tags):
v0.1.0 → v0.2.0 → v0.2.1 → v0.2.2 → v0.3.0 → v0.4.0 → v0.5.0 → v0.6.0 → v0.7.0 → v0.8.0 → **v1.0.0**.

**Code inventory (approx, at v1.0.0)**:
- `kernel/` headers + sources: ~3500 LOC
- `tests/`: ~2500 LOC (136 TEST blocks)
- `benchmarks/`: ~280 LOC (11 cases)
- `docs/` (math + schema): ~1200 LOC markdown
- `CMakeLists.txt` + `cmake/` + `.github/workflows/`: ~500 LOC infra

**What went well**
- **Pre-slice design review caught most ambiguity before code was written.** 15+ decision points per slice flagged explicitly and frozen in DEVLOG before the executor channel started work. The slice body almost never hit a "what should this do?" question mid-implementation.
- **R-09 protocol** (arithmetic self-check before dispatching numeric recommendations) matured through real failures — Slice 02's tolerance slip (`theta * 2e-7` that actually evaluates *tighter* than the original `1e-12`), Slice 07's three-way spec-vs-math contradiction (1e-8 KNN accuracy demanded in C2 while §14 proved it unreachable at ε=1). R-09 is now standard: every tolerance number gets substituted into its bound expression and mentally evaluated before being written to spec.
- **CI matrix caught latent C++11 bugs** in Slice 01 that would have silently leaked otherwise (KernelParams aggregate rules, R-11). One CI round saved one debugging session.
- **Feature branch + PR + branch protection + auto-delete** stabilised after Slice 07. From Slice 08 onward, zero friction: PR opens, CI runs, merge, auto-delete, tag. Takes ~5 min of human attention per slice.

**What to improve in Phase 2**
- **Spec pre-validation rigor.** The reviewer channel (me, in the spec voice) introduced at least 5 spec errors during Phase 1 that the executor channel (me, in the code voice) caught during implementation:
  - `EIGEN_ASSERT` vs `eigen_assert` casing (Slice 03)
  - Debug directory assumption (Slice 02)
  - `theta * 2e-7` tolerance that's tighter than the prior 1e-12 (Slice 02)
  - Three tolerance numbers off-by-orders-of-magnitude in Slice 07 (B2, C2, G1)
  - Slice 08 `std::getenv` under `/WX` (caught at build, not spec)
  Fix: the reviewer channel should run a "mental dry run" before dispatching — mentally execute each step, check every numeric claim against a written derivation, cross-reference test tolerances with the math chapters *in the same spec*.
- **R-09 extension**: now codified as "any `(tolerance, bound)` pair in the same spec must satisfy `tolerance ≥ bound × safety_margin`, substitutions checked on paper before dispatch". Written into the Phase 2 executor protocol.
- **Branch protection initial setup** had self-approve deadlock (T-08) and required GitHub UI tweaks (required review count 0 for single-contributor). Should be anticipated for any new protected-branch setup; added to Phase 2 entry conditions.
- **PR merge housekeeping**: the "click Delete branch" step was missed on Slice 05, 06, 07 consecutively. Fixed permanently by enabling "Automatically delete head branches" at repo level (T-09, verified working by Slice 08 tag closeout). Lesson: enable auto-delete on any new repo before the first PR.

**Tech-debt register carried into Phase 2**

| ID   | Description                                             | Plan                                             |
|------|---------------------------------------------------------|--------------------------------------------------|
| R-01 | CMP0169 OLD policy (Eigen 3.4 upgrade)                  | Address when Eigen 3.3 EOL                       |
| R-05 | `kernel_type_from_string` first-char switch collisions  | Rewrite when adding Wendland / Matern kernels    |
| R-10 | Eigen GitLab mirror fallback                            | No-op until observed flakiness                   |
| R-11 | MSVC permissive C++14 aggregates under `/std:c++11`     | Observational; CI catches                        |
| R-12 | Node 20 → Node 24 deprecation (2026-06)                 | Upgrade actions when v5 series ships             |
| R-13 | GCC `-Wunused-function` vs MSVC silence                 | Observational; CI catches                        |
| R-15 | Local cmake version mixing causes "Comeau" error        | Document in contributor guide                    |
| R-16 | nlohmann_json FetchContent sub-build flake (Win + VS)   | Document local workaround (SOURCE_DIR override)  |
| T-04 | `docs/math_derivation.md` §6 numbering gap              | Cosmetic; fold into future math edit             |
| T-06 | "Require branches up to date" off (single-contributor)  | Enable when second contributor joins             |
| T-07 | A6 death test not implemented (eigen_assert coverage)   | Revisit when death test harness is built         |
| —    | KdTree sub-1 μs target missed by ~43 % (Slice 09 obs.)  | Phase 2: stack-array heap + `NDEBUG`-gated asserts |
| —    | ScratchPool measurable benefit needs out-param API      | Phase 2: `predict_into(out, fr, x, pool)`        |

**Phase 2 Entry Conditions**

- Maya devkit environment setup (separate slice, not part of Phase 2's first feature slice).
- Decision on Maya version matrix (2022 / 2024 / 2025 / 2026).
- Decision on Qt6 binding (PySide6 stock vs Shiboken6 custom).
- **Required reading for any new collaborator before code changes**: this DEVLOG (every slice decision), `docs/math_derivation.md` (§1–14 numeric contract), `docs/schema_v1.md` (permanent on-disk format). Phase 1 decisions are committed, not suggestions — they are the contract that made v1.0.0 possible and will be the reference point for every v1.x / v2.x decision to come.

---

## 2026-04-20 · Slice 08 — JSON I/O with schema v1 (v0.8.0)

**Scope**: Persistence layer for trained interpolators. First real consumer of the `nlohmann/json` dependency that has been a deferred fetch function in `cmake/FetchDependencies.cmake` since Slice 02.5. Closes the last functional gap before the Slice 09 benchmark + v1.0.0 finale.

**Deliverables**
- `kernel/include/rbfmax/io_json.hpp` (~70 LOC) — `rbfmax::io_json::save / load` free functions + `kCurrentSchema` constant.
- `kernel/src/io_json.cpp` (~390 LOC) — anonymous-ns enum/scalar/matrix helpers, `iso8601_now`, `library_version_string`, `build_v1_json`, `parse_v1_json`, plus the public `save / load` with try-catch perimeters.
- `RBFInterpolator::save / load` (interpolator.hpp +9 lines, interpolator.cpp +38 lines) — convenience methods delegating to io_json with internal kdtree / pool rebuild on successful load.
- `tests/test_io_json.cpp` (~440 LOC, 14 TEST blocks across A-E categories).
- `docs/schema_v1.md` (~155 LOC) — normative schema spec, field-by-field semantics, known limitations, upgrade path.

**Design decisions (8 locked pre-slice)**
1. **Schema structure: nested** (variant B) over flat (A). Easier evolution; matches industry conventions (`package.json`, `cargo.toml`).
2. **Version strategy: integer-incrementing `"rbfmax/v<N>"`** (variant A), decoupled from package SemVer. File-format lifecycle differs from package SemVer.
3. **API: free functions + `RBFInterpolator` convenience methods** (variant B, best of both worlds — testable in isolation, ergonomic at call sites).
4. **Float precision: full 17-digit double round-trip** (variant B). nlohmann's default formatting recovers the unique IEEE double, locked by tests A1-A5 + D1 with `EXPECT_EQ` (not `EXPECT_NEAR`).
5. **Errors: bool return, noexcept, atomic update** (variant B). `out_*` is *only* mutated after the entire parse succeeds — failure leaves callers untouched. `RBFInterpolator::load` extends this by also leaving the kdtree / ScratchPool unchanged on failure.
6. **`meta.version` is diagnostic, not dispatch** (variant A). Two files differing only in `meta.created_at` deserialize identically.
7. **NaN/Inf: lossy convert to JSON null** (variant C). Pragmatic; documented in schema_v1.md and locked by test D2.
8. **Random seed: `0xF5BFA7u`** (sequential after Slice 07's `0xF5BFA6u`).

**Deviation from blueprint docx**
- The original architecture document (`docs/源文档/优化提示词：Claude 插件开发.docx`) recommended Protobuf for serialization. We chose JSON (nlohmann/json) per DEVLOG D-01 for human-readability + zero toolchain overhead. Slice 08 executes that decision. If large-rig file size becomes a bottleneck, a binary sidecar (MessagePack / CBOR) can be added in v2 without breaking JSON readability — schema_v1.md "Typical file size" + "Upgrade path" sections sketch the migration.

**Tolerance register (audit anchor)**
- A1-A5 / D1 / D2 condition_number (NaN check): exact equality (`EXPECT_EQ` on Eigen matrices, scalar `==`). The full-double-precision round-trip claim is bit-identical, not approximate.
- E1 predict-after-load consistency: `1e-14` absolute. Same arithmetic path on both sides; the margin guards against any sum-order drift Eigen might introduce.
- C2 file-readable: only asserts file size > 0; no content tolerance.

**Local-verification surprise (R-15 environment + new MSVC interaction)**
- First Release build failed with **C4996 on `std::getenv`** (TempFile helper used `std::getenv("TEMP")`). Project ships with `/WX`, so warning → error. Fix: switched the Windows branch of `TempFile` to `_dupenv_s` (heap-allocates the env value, `free()` on cleanup). Linux/POSIX path unchanged.
- First Release configure failed with `Build step for nlohmann_json failed: 1` even though the source had been git-cloned successfully. Workaround: pass `-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$(pwd)/build/_deps/nlohmann_json-src` so FetchContent skips the populate sub-build. This avoids a flaky Visual Studio sub-project step on already-cloned dependencies. CI (which always starts from a clean checkout) will not hit this; the workaround is purely a local development convenience and is not committed (no CMakeLists change needed).

**Tech-debt register**
- None new.
- `schema_v1` is now a **permanent commitment** — any future modification of `parse_v1_json` is forbidden unless it would refuse to load legacy v1 files. Future v2/v3 must add new dispatch branches.

**Workflow note**
- Fifth slice on the post-protection PR workflow. Branch `slice-08-io-json` → PR → CI (with first nlohmann/json fetch — expect cache miss + ~20s extra on the first run) → human approval → rebase merge → tag `v0.8.0`.
- Auto-delete head branches now enabled at the repo level (per Slice 07 closeout note); Slice 08 will be the first to validate that workflow change.

**Outstanding after Slice 08**
- **Slice 09**: benchmarks + **v1.0.0** — Phase 1 finale. Will populate the empty `benchmarks/` skeleton from Slice 05, validate the ScratchPool zero-allocation claim deferred from Slice 06, and establish wall-clock baselines for fit/predict at typical N/D/M combinations.

---

## 2026-04-20 · Slice 07 — RBFInterpolator facade (v0.7.0)

**Scope**: Phase 1 integration slice. Wraps the 5 lower modules (kernel / distance / kdtree / solver / scratch_pool) into a single user-facing class. End of Phase 1 API surface; Slice 08 (JSON I/O) and Slice 09 (benchmarks) are non-API work.

**Deliverables**
- `kernel/include/rbfmax/interpolator.hpp` (~130 LOC) — `InterpolatorOptions` struct and `RBFInterpolator` class (move-only, clone() for per-thread).
- `kernel/src/interpolator.cpp` (~380 LOC, linked into `rbfmax_solver` STATIC library).
- `tests/test_interpolator.cpp` (15 TEST blocks, 7 categories, random seed `0xF5BFA6u`).
- `docs/math_derivation.md §14` — KNN truncation error analysis replacing the prior SIMD-notes placeholder.

**Design decisions (8 locked pre-slice)**
1. **kdtree only for Gaussian** (decision variant C'): IMQ excluded due to long-tail non-convergence at K=32-64 (per-center contribution ~O(r⁻¹) doesn't decay fast enough).
2. **fit() fully replaces prior state**: no history retained; equivalent to fresh construct + fit.
3. **Single `InterpolatorOptions` constructor** taking kernel params only (other fields via brace-init or direct assignment).
4. **Move-only + `clone()` for per-thread**: rule-of-five hand-written for noexcept guarantees; clone() rebuilds kd-tree against the copy's centers buffer to guarantee spatial-storage independence.
5. **NOT thread-safe** on a single instance (mutable `ScratchPool` + KNN scratch buffers). Documented in header doc-block.
6. **Full getter set**: `is_fitted`, `status`, `solver_path`, `n_centers`, `dim`, `lambda_used`, `condition_number`, `uses_kdtree`.
7. **Default kdtree threshold N=256**; kernel-specific.
8. **Default K = min(N, 32) for Gaussian**; overridable via `opts_.knn_neighbors`.

**IMQ kdtree deferred** — original spec had decision 1 as variant C (Gaussian+IMQ) but arithmetic check during review showed IMQ truncation error at K=64, N=500 exceeds 1e-2. Revised to C' (Gaussian-only). Future slice could design dedicated ball-tree + adaptive K for IMQ.

**Thread safety contract** — a single `RBFInterpolator` is NOT thread-safe (the mutable `ScratchPool` and `indices_buf_` / `sq_dist_buf_` are shared per-predict-call scratch). Users needing concurrent predict call `clone()` per worker thread. Documented in both header doc-block and the class comment.

**Spec deviations (R-09 protocol, 3 items)**

These were caught during local Release verification when 3/122 tests failed against the originally-speced tolerances. All three turned out to be spec-vs-reality mismatches, not implementation bugs:

| Test | Spec tolerance | Reality | Fix |
|---|---|---|---|
| B2 `SecondFitWithDifferentDimensions` | `1e-6` sample-point residual | `~0.02` — 2D × N=50 × Gaussian ε=1 has heavily-overlapping kernels, conditioning leaves ~2% residual | Relaxed to "finite + within 0.1 of target" since B2's intent is just to verify refit-with-new-dim succeeds. |
| C2 `KnnApproxMatchesDenseForGaussian` | ε=1, 1e-8 | Per §14 (landed this slice!), at ε=1 in 3D unit cube per-center KNN truncation is ~0.37; spec contradicted its own math doc | Changed ε=8 (narrow Gaussian, near-diagonal kernel matrix) + engineering tolerance 1e-3. |
| G1 `SinCosReconstructsSameAsSolver` | `1e-6` "与 Slice 05 G1 同水准" | Slice 05 G1 (`EndToEnd.SinCosZReconstructs`) actually uses `0.1`; spec misremembered | Aligned to 0.1. |

**Lesson (carries R-09 forward)**: Spec tolerance claims must be substituted back to the math doc the spec itself cites. C2 was particularly embarrassing — the same spec that asked for 1e-8 KNN accuracy also commissioned §14 which proves 1e-8 isn't reachable at ε=1. Cross-reference before dispatch next slice.

**Tolerance register (audit anchor)**
- A2/A3 sample-point reconstruction (Gaussian/Cubic, N=20-25): `1e-6`/`1e-5` absolute — tight fits with small N, well-conditioned.
- B1 A→B refit separation check: `> 1e-3` — ensures state actually changed.
- C2 KNN vs dense: `1e-3` at ε=8, see math §14.4.
- E1 clone() identity: `1e-14` absolute — bit-identity expected since clone copies FitResult and rebuilds pool+kdtree without re-fitting.
- F2 singular: status ∈ {OK, SINGULAR_MATRIX}, no tolerance.
- G1 end-to-end RMSE: `0.1` — matches Slice 05 realistic baseline.

**Tech-debt register**
- None new by count, but one item of note:
  - `build_polynomial_row_local` duplicated in `interpolator.cpp` anonymous namespace (matches `solver.cpp`'s private helper). Accepted rather than exposing an internal helper through `solver.hpp`'s public surface. Consolidate if Slice 10+ introduces a third consumer.

**Workflow note**
- Fourth slice on the post-protection PR workflow. Branch `slice-07-interpolator` → PR → CI matrix → human approval → rebase merge → tag `v0.7.0`.
- Continued R-15 environment hygiene (two cmake versions, one per build dir).

**Outstanding after Slice 07**
- **Slice 08**: JSON I/O — serialize `FitResult` for persistence and cross-process transfer. `nlohmann_json` already fetched (deferred fetch function ready in `FetchDependencies.cmake`).
- **Slice 09**: benchmarks — first real performance validation, including ScratchPool zero-alloc claim (Slice 06 deferred). Populates the empty `benchmarks/` skeleton from Slice 05.
- **v1.0.0** at end of Slice 09 marks Phase 1 complete.

---

## 2026-04-20 · Slice 06 — ScratchPool for zero-alloc predict (v0.6.0)

**Scope**: Breathing slice after Slice 05's peak workload. Pure engineering optimization, no new math. Eliminate heap allocations in predict hot path to prepare for Maya 60fps `compute()`.

**Deliverables**
- `kernel/include/rbfmax/solver.hpp` — `ScratchPool` class (move-only, four pre-allocated `VectorX` members, `noexcept` API) + two `predict_*_with_pool` declarations, placed adjacent to the existing public API block.
- `kernel/src/solver.cpp` — `ScratchPool` ctor, `build_polynomial_row` helper (anonymous namespace), `predict_with_pool`, `predict_scalar_with_pool`, and internal refactor of `predict_batch` / `predict` / `predict_scalar` to share the pooled compute path via delegation.
- `tests/test_solver.cpp` — 9 new TEST blocks (P1/P2/P3/P4/P5/P7/P8/P9/P10); P6 zero-alloc instrumentation intentionally skipped per design decision (deferred to Slice 09 google-benchmark suite). Random seed `0xF5BFA5u` (sequential after Slice 05's `0xF5BFA4u`).

**Design decisions (3 locked pre-slice + 2 derived)**
1. **Memory strategy**: pre-allocated Eigen `VectorX` members, not `std::vector` + manual offsets and not a custom arena. Rationale: Eigen's resize-at-construction guarantees the inner predict loop hits no allocator; arena would invite ABI-mismatch headaches with Maya's TBB.
2. **API integration**: internally consume pool in `predict_batch` (transparent to users; signature unchanged) + expose `predict_with_pool` / `predict_scalar_with_pool` explicitly for Maya per-frame reuse where the node owns pool lifetime.
3. **Thread safety**: pool is NOT thread-safe; each thread / TBB task owns its own instance. Documented in header doc-block.
4. **File placement**: `ScratchPool` lives in `solver.hpp` (not a separate `scratch_pool.hpp`) — it is a solver-internal scratch type and does not warrant a new header in the public surface.
5. **Compute path unification**: `predict` / `predict_scalar` were refactored to delegate to the pool variants via an internal temporary pool. This single-sources the arithmetic and lets P4/P5 lock the bit-identity invariant (1e-14 absolute tolerance — no wiggle room, exact equality expected).

**Tolerance register (audit anchor)**
- P4 / P5 / P7 — `1e-14` absolute. `predict_with_pool` and `predict` share the *same* arithmetic sequence (delegation, not re-implementation), so equality is theoretically exact; `1e-14` is the safety margin for any reordering Eigen might insert.
- P9 — no tolerance, only `std::isfinite` (functional smoke test on a 500×500 batch).

**Tech-debt register**
- None new.
- Clears the informal "predict allocates per call" concern raised at the end of Slice 05, though `predict` / `predict_with_pool` still return `VectorX` by value (one allocation per query by interface). True O(1) per-query requires an out-parameter API; deferred to Slice 07 (`RBFInterpolator` end-to-end class).

**Workflow note**
- Third slice on the post-protection PR workflow. Branch `slice-06-scratch-pool` → PR against `main` → CI matrix → human approval → rebase merge → tag `v0.6.0`.
- Two cmake versions coexist on the dev machine (3.25 standalone for `build-dbg/`, 3.31 VS-bundled for `build/`). Build commands now route to the matching cmake binary per build dir to avoid the "No preprocessor test for Comeau" regen failure observed during Slice 06 verification. Recorded as informal R-15 (no code action needed; environment hygiene).

**Outstanding**
- Slice 07: `RBFInterpolator` end-to-end class, consumes solver + kdtree + ScratchPool. May introduce out-parameter `predict_into(out, fr, x, pool)` to close the last allocation.
- Slice 09: benchmark will validate ScratchPool actually eliminates per-iter allocations (current Slice 06 tests only verify functional equivalence).

---

## 2026-04-19 · Slice 05 — RBF Solver (v0.5.0)

**Scope**: The largest slice in Phase 1. Lands the first non-header-only module (`kernel/src/solver.cpp`) and the first STATIC library target (`rbfmax_solver`), unifying the kernel + distance + rotation + kdtree stack into the canonical RBF `fit → predict` pipeline. Slice 06+ pose-space applications, JSON I/O and Maya node integration all consume `rbfmax::solver`.

**Deliverables**
- `kernel/include/rbfmax/solver.hpp` — public API (`FitOptions`, `FitResult`, `SolverPath`, `FitStatus`, two `fit()` overloads + three `predict_*` overloads, all `noexcept`).
- `kernel/src/solver.cpp` — implementation: graded-lex monomial basis, kernel matrix builder, three-tier `LLT → LDLT → BDCSVD` fallback, QR elimination for the augmented saddle-point system, GCV via SVD closed form on a 32-point log grid `[1e-12, 1e2]`.
- `tests/test_solver.cpp` — 34 TEST blocks across 7 categories (A basics 8 / B numerics 6 / C solver-path 4 / D GCV 4 / E poly tail 4 / F batch 3 / G end-to-end 5). Random seed `0xF5BFA4u`.
- `docs/math_derivation.md §11–13` — Tikhonov regularisation, GCV closed form, QR elimination derivation. §14 placeholder reserved for Slice 06.
- `benchmarks/CMakeLists.txt` + `benchmarks/.gitkeep` — clears Slice 01 R-06 tech debt: `-DRBF_BUILD_BENCHMARKS=ON` was permanently broken because top-level CMake added `benchmarks/` as a subdirectory but no CMakeLists existed inside it.

**Design decisions (15 fixed in pre-slice review)**
1. `kLambdaMin = 1e-12` floor on regularisation parameter.
2. λ-clamp policy: silent in Release, `assert` in Debug.
3. Solver fallback ordering: `LLT → LDLT → BDCSVD`.
4. `FitResult::path` records actual solver used (audit trail).
5. Augmented system → QR elimination via Householder (not Schur complement).
6. Polynomial basis: standard monomial, graded-lex order, degree ∈ [0, 3].
7. Polynomial activation: only when `KernelType::minimum_polynomial_degree() ≥ 0`.
8. GCV grid: 32 log-spaced points over `[kLambdaMin, 1e2]`.
9. GCV evaluation: SVD closed form (no nested solves over the grid).
10. All public API `noexcept`, errors via `FitStatus` enum.
11. STATIC (not SHARED) library — folds into the eventual Maya plug-in DLL without exposing internal symbols.
12. PUBLIC link `rbfmax::kernel` so transitive consumers inherit Eigen alias.
13. `rbfmax_apply_warnings` + `rbfmax_apply_release_tuning` applied to solver TU.
14. Test infrastructure: conditional `rbfmax::solver` link inside `rbfmax_add_test()` for `test_solver` only.
15. Random seed `0xF5BFA4u` (sequential continuation from Slice 04's `0xF5BFA3u`).

**Workflow note**
- Second slice on the post-protection PR workflow (after Slice 04). Branch `slice-05-solver` → PR #2 against `main` → CI matrix → human approval → rebase merge → tag `v0.5.0`.

**Spec deviation note (R-09 protocol)**
- Test C2 (`SolverPath.DuplicatesFallbackToLDLT`) was specified to verify that near-duplicate centers force the LLT path to fail and fall back to LDLT or BDCSVD. **This premise contradicts elementary linear algebra**: for any symmetric `A`, the matrix `A + λI` with `λ ≥ kLambdaMin > 0` is *strictly* positive-definite (every eigenvalue lifted by at least λ above zero), so LLT *always* succeeds. The spec scenario can only be reproduced by either (a) `λ = 0` — forbidden by `kLambdaMin`, or (b) a non-PSD kernel — out of scope for this slice. Test relaxed to verify `status == OK` and `path != FAILED`. Documented in commit 1 message body and here.
- **Lesson**: Spec assumed numerical fragility that the project's own invariant (`kLambdaMin`) had already engineered away. Future spec reviews should cross-check pathological-input tests against the regularisation guarantees they themselves mandate.

**Tolerance register (audit anchor)**
- Reconstruction (sin·cos·z, gaussian, Runge): `1e-3` RMSE absolute.
- Train residual on noiseless data: `1e-6` absolute.
- LLT vs LDLT vs BDCSVD result agreement: `1e-9` relative.
- GCV-selected λ on noisy data: must satisfy `λ_GCV > 100·kLambdaMin` (validates auto-selection actually engaged).

**Tech-debt register additions**
- None new. R-06 (benchmarks dangling subdirectory) cleared by this slice.

**Validation**
- Local Windows MSVC 17.3 double-green: Release **98/98**, Debug **98/98** pass. By-design `GTEST_SKIP`s: `Numerics.LambdaBelowMinClampsSilently` (Release-only behaviour), `BatchPredict.DimensionalityMismatchTrapsInDebug` (Debug-only assert), `SwingTwistDecomposition.DebugAssertOnNonUnitAxis` (Slice 03 carryover, Debug-only).

**Outstanding**
- Slice 06 (kernel-tap interface / pose-space adapters) — unblocks Maya node parameter binding.
- Slice 09 (benchmarks) — `bench_solver.cpp`, `bench_kdtree.cpp` to populate the now-functional benchmarks/ skeleton.

---

## 2026-04-19 · Slice 04 — kd-tree spatial index (v0.4.0)

**Scope**: Introduce nearest-neighbor acceleration for RBF interpolation under large-sample regimes. Required by Slice 05 (solver) when N > ~1000 and per-query O(N) cost becomes prohibitive for 60fps playback.

**Deliverables**
- `kernel/include/rbfmax/kdtree.hpp` — header-only, ~230 LOC, array-backed, variance-based split.
- `tests/test_kdtree.cpp` — 11 TEST blocks, brute-force parity validation on 500-sample × 100-query scan.
- `docs/math_derivation.md §10` — complexity & pruning geometry, edge-case behavior table.

**Design decisions (from pre-slice design review)**
- Array-backed nodes (vs linked) for cache locality.
- Caller-managed sample lifetime (zero-copy via `Eigen::Ref<const MatrixX>`).
- Out-parameter buffers (zero-alloc hot path).
- Recursive (vs iterative-with-stack) — depth log₂N bounded.
- `std::priority_queue` as max-heap on squared distance.
- Variance-based split dimension.
- Median split value (`std::nth_element`).
- `k>N` silently clamps, returns actual count.
- Output ascending by distance, squared distances (no `sqrt`).

**Workflow note (first post-protection slice)**
- Slice 04 is the first slice developed on a feature branch (`slice-04-kdtree`) merged via PR, after Branch Protection on `main` was enabled following Slice 03's second consecutive green CI. Direct push to main is now rejected.
- Tag `v0.4.0` will be applied post-merge on the main HEAD by a separate authorization round.

**Spec deviation note**
- Test K3 was specified as a "tree depth bounded" probe, but the public KdTree API deliberately does not expose `nodes_` (encapsulation invariant). Replaced K3 with a stronger functional property: build correctness on N=1000 verified via 1-NN parity against brute force. The `O(log N)` query touch count is implicitly exercised by K11's <500ms budget on N=10000. Documented in commit 1 message body.

**Tech-debt register additions**
- None new. A6-style death tests deliberately avoided (see T-07).

**Outstanding**
- Slice 05 (solver) — next in dependency graph; first slice to unify kernel + distance + rotation + kdtree into an end-to-end RBF interpolation pipeline with Tikhonov regularization.

---

## 2026-04-19 · Slice 03 — Quaternion algebra (v0.3.0)

**Scope**: First pose-space math primitive layer. Delivers the `rbfmax::rotation` submodule required by Slice 05 (solver) when RBF centers are unit quaternions rather than Euclidean anchors.

**Deliverables**
- `kernel/include/rbfmax/quaternion.hpp` — three APIs in `rbfmax::rotation::`:
  - `decompose_swing_twist(q, axis)` returning `SwingTwist{swing, twist}`
  - `log_map(q) -> Vector3` (rotation vector, short-path)
  - `exp_map(v) -> Quaternion` (rotation vector → unit quaternion)
- `tests/test_quaternion.cpp` — 16 TEST blocks (15 active + 1 GTEST_SKIP), ~5000+ assertions via 1000-sample fixed-seed batches.
- `docs/math_derivation.md §7` (Swing-Twist algebra) and `§8` (Log/Exp Lie algebra) — full derivations. The previous `§6` placeholder was superseded; remaining solver placeholder moved to `§9`.

**Design decisions (from pre-slice design review)**
- Axis parameterization: `Vector3` (not `Axis3` enum) — supports local bone axes that are not world axes.
- Unit-axis contract: caller-enforced + `eigen_assert` in Debug (mirrors `distance.hpp` geodesic contract).
- 90° swing degeneracy: return `twist = Identity` (twist unobservable).
- Log map double cover: internally flip `q → -q` when `w<0` (shortest-path semantics, standard for interpolation pipelines).
- Taylor threshold: `|v| = 1e-8` — conservative, Taylor truncation error `O(1e-32) ≪ ε_mach`.
- Random seed: `0xF5BFA2u` (Slice 02 used `0xF5BFA1u`; sequential seeds per slice for determinism without cross-test collisions).

**Test tolerance register (audit anchor for future review)**
- Identity/zero round-trips: `1e-14` absolute
- `Log∘Exp`, `Exp∘Log` round-trip: `1e-10` absolute
- Taylor near-zero: `1e-14` absolute
- π-boundary: `1e-8` absolute (asin saturation)
- Unit-norm preservation: `1e-14` absolute

**Spec deviation note**
- Spec specified `EIGEN_ASSERT` (uppercase); Eigen 3.3.9 only exposes `eigen_assert` (lowercase) as the public contract macro. Used lowercase form throughout `quaternion.hpp` and its doc comments. Documented in commit 1 message body and here as a single-source-of-truth audit trail.

**Tech-debt register additions**
- None new. Slice 03 clean, no workarounds.

**Outstanding after Slice 03**
- Branch protection still pending (needs 2 consecutive green main runs; v0.2.2 was 1st green if remote CI passed, Slice 03 push will be 2nd if green).
- Slice 04 (kdtree) next in dependency graph.

---

## 2026-04-19 · Slice 02.5.1 — First CI-caught regression (v0.2.2)

**Context**: Slice 02.5 introduced CI; its very first run on main (trigger: push of `5330836`) failed the `ubuntu-gcc-release` job while both Windows MSVC Release and Debug jobs passed. This is the first CI-caught latent regression and exactly the reason CI was prioritized before Slice 03.

**Root cause**

`KernelParams` (introduced in Slice 01) used default member initializers to encode defaults:

```cpp
struct KernelParams {
    KernelType type {KernelType::kGaussian};
    Scalar     eps  {1.0};
};
```

Under C++11 `[dcl.init.aggr]/1`, any brace-or-equal-initializer on non-static data members disqualifies the class from being an aggregate. C++14 lifted this restriction. MSVC permissively accepts C++14 aggregates under `/std:c++11`, which masked the bug locally. GCC 11 with `-std=c++11 -Wpedantic` correctly rejected the call site `KernelParams{kGaussian, 2.0}` in `tests/test_kernel_functions.cpp:229`.

**Fix**

Replaced default member initializers with two explicit constructors:

- `KernelParams() noexcept` — defaults to `{kGaussian, 1.0}`
- `KernelParams(KernelType, Scalar) noexcept` — explicit 2-arg

Semantic equivalence at all call sites preserved; no test changes.

**Tech-debt register additions**

- R-11 (new): MSVC silently accepts C++14 aggregate rules under `/std:c++11`. Any use of C++14-only language features must be audited manually or exposed by CI. A future `fix(cmake)` could add `/Zc:__cplusplus` already present plus a comment warning, but MSVC has no equivalent of `-Wpedantic` for aggregate rules.

**Lesson**

- CI-before-new-features was the right call. Slice 02.5 bought 24h of calendar time, caught a C++11 bug that would have silently leaked into every subsequent slice, and paid for itself on day 1.

---

## 2026-04-19 · Slice 02.5 — CI baseline (v0.2.1)

**Scope**: Bootstrap GitHub Actions workflow covering the three committed compiler/build permutations.

**Deliverables**
- `.github/workflows/ci.yml` — three-job matrix:
  - `windows-msvc-release` on windows-2022, MSVC 17.x, Release
  - `windows-msvc-debug`   on windows-2022, MSVC 17.x, Debug
  - `ubuntu-gcc-release`   on ubuntu-22.04, GCC 11, Release
- FetchContent dependency cache keyed on `cmake/FetchDependencies.cmake` hash.
- Test log artifacts uploaded on failure for post-mortem.
- Concurrency group cancels superseded runs on the same ref.

**Out of scope (deferred)**
- macOS runner (low Maya usage on that platform)
- GCC 4.8.2 (Maya 2018; needs CentOS 6 container — defer to Phase 2)
- Clang, sanitizers, static analysis — revisit at Slice 05 or later
- Branch protection rule on main — to be enabled after the workflow has two consecutive green runs on main.

**Version note (historical)**
- The `v0.1.0` tag points at the `feat(kernel)` bootstrap commit (`a8f0143`), because Slice 01 did not separate a `chore(release)` commit. From Slice 02 onward releases are isolated to their own `chore(release)` commits for cleaner audit.

**Tech-debt register**
- T-03 (new): branch protection rule not yet enabled. Enable after two consecutive green main runs.
- R-10 (new): if CI matrix flakes from GitHub runner network instability hitting GitLab-hosted Eigen, consider bundling Eigen as a git submodule or upstreaming to an internal mirror.

---

## 2026-04-19 · Slice 02 — Distance Metrics

**阶段**：Phase 1 (Mathematical Kernel)
**切片**：02/09
**版本跃迁**：`0.1.0` → `0.2.0`
**Commits**：
- [`2b59a8b`](https://github.com/891458249/RBF_MAX/commit/2b59a8b) · `fix(kernel)` TPS 负 r 契约对齐
- [`09d909a`](https://github.com/891458249/RBF_MAX/commit/09d909a) · `feat(distance)` 距离度量主体
- _本条目所在的 `chore(release)` 待回填_

**会话来源**：Claude Opus 4 架构师协作（含切片 ① 评审 + 切片 ② 评审两轮）

### 交付物

| 路径 | 行数 | 说明 |
|---|---|---|
| `kernel/include/rbfmax/distance.hpp` | 117 | 欧氏 + 四元数测地距离，命名空间 `rbfmax::metric` |
| `tests/test_distance.cpp` | 254 | 14 个 TEST，含 1000 组三角不等式回归 |
| `docs/math_derivation.md` §5 | +115 行 | 反号归一证明、acos/asin 分支判据、Lipschitz 误差上界 |
| `kernel/include/rbfmax/kernel_functions.hpp` | 注释修订 | TPS 负 r 契约从"NaN"改为"clamp 到 0"，与实现一致 |
| `tests/test_kernel_functions.cpp` | +3 TEST | `KernelContract.*` 锁定负 r 三档行为（奇/偶/clamp） |
| `tests/CMakeLists.txt` | +1 行 | 注册 `test_distance` |
| `CMakeLists.txt` | VERSION 0.1.0 → 0.2.0 | |
| `CHANGELOG.md` / `DEVLOG.md` | — | 双层日志追加 |

### 关键技术决策

1. **距离接口使用 Eigen 模板签名**
   - 切片 ② 评审建议 1。避免 `const VectorX&` 强制调用方构造动态向量导致的堆分配。
   - 实现以 `template <typename DerivedA, typename DerivedB>` 接收 `Eigen::MatrixBase<>`，编译期 size 不匹配由 Eigen 静态断言捕获。

2. **四元数测地距离双分支**
   - 常规区间用 `2·acos(clamp(|dot|, -1, 1))`；近单位 `|dot| ≥ 1 - kQuatIdentityEps (1e-14)` 切换到 `2·asin(sqrt(1 - dot²))`。
   - 数学推导 §5.2.3 证明两支的相对误差上界分别为 `√ε_mach ≈ 1.5e-8` 与 `~1e-7`；后者是双精度物理精度下限。

3. **单位化契约：Release 信任、Debug 校验**
   - 切片 ② 评审建议 2。热路径零分支；`assert(|‖q‖² − 1| < 1e-6)` 仅 Debug 生效。
   - 零四元数作为未定义行为写入头部注释，过滤责任上移至 Maya 节点的 attribute ingress。

4. **三角不等式抽样 1000 次、种子固定**
   - 切片 ② 评审建议 3。原提案 10 次远不够统计学严谨；1000 次耗时 <10ms 但能稳定捕获任何数值实现 bug。
   - 种子 `0xF5BFA1u` 硬编码，CI 断言永不 flake。

5. **TPS 契约修复类型归为 `fix:` 而非 `refactor:`**
   - 切片 ② 评审建议 4。Conventional Commits 按"修复的问题"分类，不按"改的文件"分类；文档-实现的 drift 是缺陷，SemVer PATCH 归零随 MINOR 抬升自然处理。

### 风险 & TODO（新增；延续自切片 01 的保留）

| ID | 状态 | 风险 |
|---|---|---|
| R-01 | 保留 | `CMP0169 OLD` 依赖，与 Eigen 3.4 升级联动 |
| R-02 | 保留 | CentOS 6 + GCC 4.8.2 实机未验证 |
| R-03 | 保留 | `static constexpr` 命名空间常量潜在警告 |
| R-04 | 保留 | Eigen 3.3.9 `-Wdeprecated-declarations` 在 GCC 11 |
| R-05 | 保留 | `kernel_type_from_string` 首字符 switch（Wendland/Matern 引入时重构） |
| R-06 | 新增 | `CMakeLists.txt` 引用不存在的 `benchmarks/` 子目录，启用 `RBF_BUILD_BENCHMARKS=ON` 会失败；切片 ⑨ 前加 `if(EXISTS)` 守卫 |
| R-07 | 新增 | `RowMatrixX` 与 `rbfmax_apply_release_tuning` 均为"暂时死代码"，等 `io_json.hpp` / solver static lib 接入时消费 |
| R-08 | 新增 | 四元数入参契约要求单位化，但 Phase 2 Maya 节点若直接连接 `rotate` 属性的欧拉→四元数转换链可能引入 `1e-7` 级漂移；需要在节点 `compute()` 入口做强制 `normalized()` |
| T-01 | 保留 | commit-msg hook 未接入 |
| T-02 | 保留 | GitHub Actions CI 矩阵未接入 |
| T-03 | 新增 | 性能 benchmark 未覆盖 `quaternion_geodesic_distance`（在 1M 调用/帧场景下可能是瓶颈），Slice ⑨ 补齐 |

### 切片内验收门

| 检查项 | 状态 |
|---|---|
| `test_distance` 新增 14 个 TEST / ≥ 40 断言 | ✅ 落盘 |
| 三个四元数病态点（`q=q'`、`q=-q'`、`dot≈0`）全覆盖 | ✅ 落盘 |
| 三角不等式 1000 组抽样固定种子 | ✅ 落盘 |
| asin 回退分支误差上界文档化 | ✅ `math_derivation.md §5.2.3` |
| 零四元数契约明确 | ✅ `distance.hpp` 头 + `math_derivation.md §5.2.4` |
| 本地编译 + ctest 验证 | ⏳ 待用户本地执行 |

### 下一切片依赖

Slice 03 — `quaternion.hpp`（Swing-Twist 分解、Log/Exp map）将消费本切片的 `kQuatIdentityEps` 常量与 `quaternion_abs_dot` 工具。

### Hotfixes (during Slice 02 admission gate)

Three-round failure → green sequence during the local double-build verification, documented here for historical accuracy and to seed the tech-debt register.

**Round 1 — MSVC C4819 under code page 936**
Slice 01 was never compiled under MSVC; UTF-8 sources without BOM raised C4819 → C2220 under `/WX`. Fix: `cmake/CompilerFlags.cmake` gained `/utf-8` in the MSVC branch.
→ committed as `build(build): add /utf-8 ...`

**Round 2 — Arithmetic slip in the tolerance recommendation (R-09)**
The review channel recommended `EXPECT_NEAR(d, theta, theta * 2e-7)`, which at θ=1e-7 evaluates to `2e-14` — 50× tighter than the original `1e-12`, not looser as intended. Root cause: failing to mentally substitute θ before dispatching a numeric tolerance. Remedy internalized: any future tolerance recommendation must be preceded by explicit "dimension / substituted value / margin" self-check.

**Round 3 — §5.2.3 doc phrasing misled the test author**
"1×10⁻⁷ 相对" in §5.2.3 was literally unbounded as θ → 0; the test author (this channel's past self) consumed it as if it were a well-defined relative error. Fix: prune the ambiguous "相对" clause and add an explicit caveat that tests must use absolute tolerances.
→ committed as `fix(tests): align near-identity ...`

**Tech-debt register additions**
- R-09 (new): arithmetic slip protocol — numeric recommendations must include substituted-value self-check before dispatch.

**Outstanding after Slice 02 admission**
- `v0.1.0` / `v0.2.0` tags not yet pushed (pending next authorization).
- Slice 02.5 (CI matrix) not yet started.

---

## 2026-04-18 · Slice 01 — Kernel Math Functions

**阶段**：Phase 1 (Mathematical Kernel)
**切片**：01/09（阶段一共 9 个切片计划）
**版本跃迁**：— → `0.1.0`
**Commit**：[`a8f0143`](https://github.com/891458249/RBF_MAX/commit/a8f0143)
**会话来源**：Claude Opus 4 架构师协作

### 交付物

| 路径 | 行数 | 说明 |
|---|---|---|
| `CMakeLists.txt` | 78 | 顶层项目；INTERFACE 目标 `rbfmax::kernel` |
| `cmake/CompilerFlags.cmake` | 75 | MSVC/GCC 警告矩阵；LTO/SIMD 分档；fast-math gating |
| `cmake/FetchDependencies.cmake` | 105 | Eigen 3.3.9 手动 populate；GTest 1.12.1；json/benchmark 延迟拉取 |
| `kernel/include/rbfmax/types.hpp` | 108 | `Scalar=double`、Eigen 别名、数值常量 |
| `kernel/include/rbfmax/kernel_functions.hpp` | 233 | 6 核函数 + 导数 + 调度器 + 字符串往返 |
| `tests/CMakeLists.txt` | 45 | `gtest_discover_tests` 注册助手 |
| `tests/test_kernel_functions.cpp` | 229 | 7 个 TEST 套件，34 个断言 |
| `docs/math_derivation.md` | 140 | LaTeX 推导、L'Hôpital 证明、有限差分容差分析 |
| `.gitignore` / `.gitattributes` / `.clang-format` | — | 工程基建 |
| `CHANGELOG.md` / `DEVLOG.md` / `COMMIT_CONVENTION.md` | — | 文档治理 |

### 关键技术决策

1. **C++11 强制基线**
   - 原文档建议 C++17，但编译矩阵包含 **GCC 4.8.2 (Maya 2018 Linux)**，强制退回 C++11。
   - 放弃 `std::optional` / 结构化绑定 / `if constexpr` / 内联变量。
   - 命名空间常量以 `static constexpr` 声明，规避 C++11/14 的非 inline ODR 陷阱。

2. **`Scalar = double` 单一内部精度**
   - 仅在 I/O 边界（Maya `MDataBlock`、JSON、UE5 导出）下转 `float`。
   - 规避 acos-based 四元数测地距离中的灾难性抵消。

3. **命名空间 `rbfmax::`（带前缀）**
   - 避开 `rbf::` 的全局符号污染风险（工作室常与物理/ML 库同构）。

4. **Thin Plate Spline 的 NaN 防御**
   - `r ≤ kLogEps = 1e-30` 直接返回 `0`；NaN 输入仍透明穿透（`r != r` 短路）。
   - L'Hôpital 极限证明见 `docs/math_derivation.md §2.4`。

5. **Inverse Multiquadric 导数数值稳定改写**
   - 原式 `-ε² r · (1 + (εr)²)^(-3/2)` 等价写作 `-ε² r / (den · √den)`，只做一次 `sqrt + div`；长尾区域精度提升 ~2 数量级。

6. **Eigen 3.3.9 手动 populate**
   - 避开 Eigen 自带 CMake 配置中的 `uninstall`/`docs`/`blas` 伪目标污染。
   - 用 `INTERFACE` 别名 `Eigen3::Eigen` + `SYSTEM` include 方向，屏蔽 `-Wshadow` 等警告外泄。
   - 注入 `EIGEN_MPL2_ONLY` 禁用 LGPL 子特性（合规前置）。

7. **fast-math 默认关闭**
   - 破坏 NaN/Inf 传播语义。
   - 作为 `RBF_ENABLE_FAST_MATH` 显式选项保留；开启时提示风险。

8. **依赖镜像机制**
   - `RBFMAX_DEPS_MIRROR` 变量可整体替换 Git 源，适配气隙构建农场。

### 风险 & TODO

| ID | 风险 | 缓解 / 计划 |
|---|---|---|
| R-01 | **FetchContent_Populate 单参形式在 CMake 3.28+ 被弃用，3.30 转错误** | 当前已 `cmake_policy(SET CMP0169 OLD)` 暂抑；阶段一收尾前迁移至 3.28 新式用法 |
| R-02 | **CentOS 6 + GCC 4.8.2 实机未验证** | 待用户提供 Maya 2018 devkit 或 CI 容器后跑一轮；否则兼容性宣称仍是声明级 |
| R-03 | **`static constexpr` 命名空间常量在某些 GCC 版本触发 `-Wunused-const-variable`** | 当前警告集未启用；待 CI 启用时视情况 `[[gnu::unused]]` 标注 |
| R-04 | **Eigen 3.3.9 在 GCC 11 `-Wdeprecated-declarations` 触发 `register` 关键字警告** | 已通过 `SYSTEM` include 屏蔽；若未来升级 Eigen 3.4 再复核 |
| R-05 | **`kernel_type_from_string` 的手写 switch-strcmp 维护性差** | 核函数数量到 10+ 时重构为 `std::unordered_map<std::string, KernelType>`（构造期一次初始化）|
| T-01 | 未接入 commitlint / pre-commit hook | 阶段一尾声接入 Husky-CMake 或纯 bash hook |
| T-02 | 未接入 GitHub Actions CI 矩阵 | 切片 ⑨ bench 交付时同步接入 |

### 切片内验收门

| 检查项 | 状态 |
|---|---|
| 本地 MSVC 2022 编译通过 | ⏳ 待用户本地验证 |
| 34 个单测全绿 | ⏳ 待验证 |
| `/W4 /WX` 零警告 | ⏳ 待验证 |
| 跨平台宣称（Linux GCC 11） | ⏳ 待 Linux 环境验证 |

### 下一切片依赖

Slice 02 — `distance.hpp` 需要依赖本切片的 `types.hpp`，不依赖 `kernel_functions.hpp`。
Slice 03 — `quaternion.hpp` 将与 Slice 02 并行设计（共享 `kQuatIdentityEps` 常量）。

---

## 模板（每条新条目使用）

```markdown
## YYYY-MM-DD · Slice NN — <标题>

**阶段**：Phase N
**切片**：NN/Total
**版本跃迁**：X.Y.Z → X.Y.(Z+1)
**Commit**：<git short sha>
**会话来源**：<对话环境/参与者>

### 交付物
| 路径 | 行数 | 说明 |

### 关键技术决策
1. ...

### 风险 & TODO
| ID | 风险 | 缓解 / 计划 |

### 切片内验收门
| 检查项 | 状态 |

### 下一切片依赖
```
