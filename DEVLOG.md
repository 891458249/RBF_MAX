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
