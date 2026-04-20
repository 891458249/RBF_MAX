# Changelog

本文件记录 **对外可见** 的变更，面向使用 RBF_MAX 插件的 TD / 动画师 / 下游管线工程师。
内部重构、测试、CI、文档等变更请查阅 [`DEVLOG.md`](./DEVLOG.md)。

格式遵循 [Keep a Changelog 1.1.0](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循
[语义化版本 2.0.0](https://semver.org/lang/zh-CN/)。

---

## [Unreleased]

_尚无未发布变更。_

---

## [0.7.0] — 2026-04-20

**Phase 1 · Slice 07 — RBFInterpolator 门面类（Phase 1 收官集成切片）**

Phase 1 集成切片：将 kernel / distance / kdtree / solver / scratch_pool
五个底层模块封装进单一 `rbfmax::RBFInterpolator` 类。用户不再需要直接
管理 `FitResult` 或 `ScratchPool`；构造一个 `RBFInterpolator`、设置
options、`fit()` 然后 `predict()` 即可。

### 新增 (Added)

- **`rbfmax::RBFInterpolator`** — 单类门面，组合 kernel + distance +
  kdtree + solver + ScratchPool，提供完整 fit→predict 生命周期：
  - `fit(centers, targets, lambda)` 与 `fit(centers, targets, kLambdaAuto)`
    两个重载。
  - `predict_scalar` / `predict` / `predict_batch` 三个查询接口。
  - 完整 getter 集：`is_fitted` / `status` / `solver_path` / `n_centers` /
    `dim` / `lambda_used` / `condition_number` / `uses_kdtree`。
  - `clone()` 用于跨线程深拷贝（rebuild kd-tree against the COPY's
    centers buffer，保证两个实例完全独立）。
- **`rbfmax::InterpolatorOptions`** — 集中配置（kernel、poly_degree、
  kdtree_threshold、knn_neighbors、force_dense）。
- **可选 kd-tree KNN 加速**：仅对 Gaussian 核启用（N ≥ 256 默认阈值）。
  其他核（Linear / Cubic / Quintic / TPS / IMQ）始终全量遍历，依据
  见 math §14。
- **数学推导 §14**：KNN 近似 RBF 的截断误差分析，明确 Gaussian 衰减
  为何足以支持 KNN，以及为何其他核不适用。
- **15 个新测试**（test_interpolator.cpp，A-G 七类），项目测试总数
  达 122 个。随机种子 `0xF5BFA6u`。

### 工程 (Build)

- 顶层 CMake 项目版本 `0.6.0 → 0.7.0`。
- `kernel/src/interpolator.cpp` 加入 `rbfmax_solver` STATIC 库源列表
  （不新建 target）。`tests/CMakeLists.txt` 注册 `test_interpolator`，
  并在 `rbfmax_add_test()` helper 内增加条件链接 rbfmax::solver。

### 契约 (Contracts)

- **NOT thread-safe**：单个 `RBFInterpolator` 实例不是线程安全的
  （内部持有 mutable `ScratchPool` 与 KNN 缓冲）。多线程 predict 必须
  用 `clone()` 给每个线程一个独立副本。
- **fit 完全替换**：`fit()` 完全替换之前的训练状态，无历史保留。
  支持 UI 工作流（样本变化时直接 refit）。
- **kdtree 启用条件**：`kernel == kGaussian` && `n_centers >= kdtree_threshold`
  && `!force_dense`。其他情况一律走 dense 全量。

### 不变 (Unchanged)

- Slice 01–06 全部 API 二进制兼容。Interpolator 通过 `#include` 组合
  调用底层模块，不修改它们。

### Phase 1 进度

- 8/9 切片完成（89%）。
- 剩余：Slice 08（JSON I/O）、Slice 09（benchmarks，含 ScratchPool 零分配
  验证）。
- v1.0.0 在 Slice 09 收官时发布。

---

## [0.6.0] — 2026-04-20

**Phase 1 · Slice 06 — ScratchPool 零分配 predict（Breathing Slice）**

呼吸切片，纯工程优化、无新数学。引入 `rbfmax::solver::ScratchPool` 为
predict 热路径提供预分配缓冲，消除内层核函数评估循环里的堆分配。
为 Maya 60fps `compute()` 做准备：单帧 predict 调用不应给 allocator
施压。

### 新增 (Added)

- **`rbfmax::solver::ScratchPool`**：预分配四个 Eigen `VectorX` 成员
  （`query_vec`、`kernel_vals`、`diff_vec`、`poly_vec`），构造时一次
  完成 sizing；`predict_with_pool` 写入这些缓冲不再分配。Move-only
  （拷贝构造/赋值已 `delete`），编译期 `static_assert` 验证。
- **`predict_with_pool(fr, x, pool)`** 与 **`predict_scalar_with_pool(...)`**
  两个新重载：暴露调用方持有 pool 生命期的零分配热路径，目标场景为
  Maya 节点的 per-frame `compute()`。
- 9 个新测试块（H 类别），随机种子 `0xF5BFA5u`（继 Slice 05 的
  `0xF5BFA4u` 之后）：覆盖构造、move 语义、与 non-pool 重载的逐 bit
  等价、N=500/K=500/M=3 大批量稳定性、零多项式尾、copy-deleted 静态
  断言。

### 变更 (Changed)

- **`predict_batch`** 内部现在创建一个 pool 并跨循环复用，per-query
  分配从 O(3N) 降至 O(1) 摊销。**公开签名未变**。
- **`predict`** 与 **`predict_scalar`** 现在通过内部临时 pool 委托到
  pool 版本实现，统一计算路径。算术与 v0.5.0 逐 bit 等价（H4/H5
  测试用 1e-14 容差锁定该不变量）。

### 不变 (Unchanged)

- Slice 01–05 全部 API 二进制兼容。
- 公开签名（`fit`、`predict`、`predict_scalar`、`predict_batch`、
  `FitOptions`、`FitResult`、`SolverPath`、`FitStatus`、`kLambdaMin`、
  `kLambdaAuto`）零变化。

### 已知限制

- 真正的零分配验证（per-iteration allocation count）推迟至 Slice 09
  google-benchmark 套件；Slice 06 测试只验证功能等价。
- `predict` / `predict_with_pool` 仍按值返回 `VectorX`，每次查询有 1
  次接口性分配。彻底 O(1) per-query 需要 out-parameter API，预留给
  Slice 07（`RBFInterpolator` 端到端类）。

---

## [0.5.0] — 2026-04-19

**Phase 1 · Slice 05 — RBF 求解器（RBF Solver）**

Phase 1 最大切片，落地项目首个**非 header-only 模块**与首个 STATIC
库 `rbfmax_solver`。RBF 插值的核心 fit→predict 流水线就此成形，
后续 Slice 06+ 的姿态空间应用、JSON 序列化、Maya 节点对接均依赖
本切片提供的求解器 API。

### 新增 (Added)

- **`rbfmax::solver` 子库**（`kernel/include/rbfmax/solver.hpp` +
  `kernel/src/solver.cpp`）：
  - `fit(centers, targets, options, lambda)`：定 λ Tikhonov 正则解。
  - `fit(centers, targets, options)`：GCV 自动 λ 选择，基于 SVD 闭式
    评分函数在 32 点对数网格 `[1e-12, 1e2]` 上扫描，无嵌套求解。
  - `predict_scalar` / `predict` / `predict_batch`：单点与批量评估，
    复用训练阶段的核参数与多项式基。
  - 三档求解回退：`LLT → LDLT → BDCSVD`，结果通过
    `FitResult::path` 暴露给调用方做数值健康度审计。
  - 多项式尾：标准单项式基，graded-lex 排序，degree ∈ [0, 3]，按
    `KernelType` 的 `minimum_polynomial_degree()` 自动激活。
  - λ 下界 `kLambdaMin = 1e-12`：低于此值在 Release 静默 clamp，
    Debug 触发 assert（R-09 设计裁定）。
  - 全部公共 API `noexcept`：失败通过 `FitStatus` + 空权重表达，
    禁止异常穿透到 Maya 节点 compute 边界。
- **`docs/math_derivation.md` §11–13**：
  - §11 — Tikhonov 正则化：变分形式、闭式法方程、SVD 收缩解释、
    `kLambdaMin` 由双精度 ε × 矩阵 Frobenius 范数推导。
  - §12 — Generalised Cross-Validation：影响矩阵 `S(λ)`、SVD 闭式
    重写、对数网格选取依据。
  - §13 — QR elimination：鞍点系统 → Householder QR 分解 → 在
    `Pᵀ` 零空间上的 SPD 子问题，并给出条件数界。
- **`tests/test_solver.cpp`**：34 个 TEST 块，7 个分类（A 基础接口 8 /
  B 数值 6 / C 求解路径 4 / D GCV 4 / E 多项式尾 4 / F 批量 3 / G 端到
  端 5），固定种子 `0xF5BFA4u`，可复现。
- **`benchmarks/CMakeLists.txt`** + **`benchmarks/.gitkeep`**：清还
  Slice 01 遗留的 R-06 技术债——`-DRBF_BUILD_BENCHMARKS=ON` 之前
  因为 benchmarks/ 目录没有 CMakeLists.txt 而永久断链。本切片补齐
  骨架，自动发现 `bench_*.cpp`（当前无文件，Slice 09 落实）。

### 工程 (Build)

- 顶层 CMake 项目版本 `0.4.0 → 0.5.0`。
- 新增 STATIC 目标 `rbfmax_solver`（别名 `rbfmax::solver`），PUBLIC
  link `rbfmax::kernel`，沿用项目严格警告集与 Release 调优 profile。
- `tests/CMakeLists.txt`：`rbfmax_add_test()` 内对 `test_solver`
  目标条件链接 `rbfmax::solver`，其它 header-only 测试零额外链接。

### 契约 (Contracts)

- λ 下界：`fit()` 入口处 `lambda < kLambdaMin` ⇒ Release clamp 至
  `kLambdaMin`，Debug `assert`。
- 解算路径：`A + λI`（`λ ≥ kLambdaMin`）始终严格正定，因此 LLT 总
  是首次成功；LDLT/BDCSVD 路径仅在数值病态超出 LLT 的 pivoting
  容忍时才被激活。
- 多项式尾：`degree < 0` ⇒ 不激活多项式基（仅核函数）；
  `degree ∈ [0, 3]`；`degree > 3` 触发 `kInvalidOptions`。

### 验证

- 本地 Windows MSVC 17.3 双绿：Release **98/98**、Debug **98/98**
  通过（Release 与 Debug 各有 1 个按设计 `GTEST_SKIP` 的反向断言
  测试，加上 Slice 03 既有的 1 个共享 skip）。

### 已知限制

- 求解器尚未接入 Maya 节点（Slice 09+/Phase 2）。
- benchmarks 仅有骨架，实际 `bench_*.cpp` 推迟至 Slice 09。

---

## [0.4.0] — 2026-04-19

### Added
- `rbfmax::spatial::KdTree` — header-only array-backed Euclidean
  kd-tree with k-nearest-neighbor search. Prepares RBF interpolation
  for large-sample (>1000) pose graphs by reducing per-query cost
  from O(N) to expected O(k log N).
- `docs/math_derivation.md` §10 — kd-tree geometry, complexity
  analysis, pruning rule derivation.
- `tests/test_kdtree.cpp` — 11 TEST blocks covering construction
  edge cases, brute-force parity, output contracts.

### Unchanged
- All Slice 01-03 APIs (kernel, distance, rotation) remain
  bit-identical.

---

## [0.3.0] — 2026-04-19

### Added
- `rbfmax::rotation` submodule with Swing-Twist decomposition, Log/Exp
  maps between SO(3) and its Lie algebra ℝ³. Provides the quaternion
  algebra primitives required by the upcoming solver slice for
  pose-space RBF interpolation.
- `docs/math_derivation.md` §7 Swing-Twist algebra and §8 Log/Exp Lie
  algebra — full derivations including singularity handling and
  Taylor-threshold error analysis.
- `tests/test_quaternion.cpp` — 16 test blocks (1 GTEST_SKIPped),
  ~5000+ assertions via 1000-sample fixed-seed random batches.
  Covers decomposition reconstruction, short-path double cover,
  Taylor branches, and π-boundary precision.

### Unchanged
- All previously released APIs (kernel functions, distance metrics)
  remain bit-identical. This slice is purely additive.

---

## [0.2.2] — 2026-04-19

### Fixed
- `KernelParams` now provides explicit constructors instead of relying on
  C++14 aggregate rules with default member initializers. Restores strict
  C++11 compliance and unblocks GCC 11 builds under `-std=c++11 -Wpedantic`.
  First regression caught by the Slice 02.5 CI baseline.

---

## [0.2.1] — 2026-04-19

### Added
- Continuous integration baseline via GitHub Actions: three-job matrix
  covering Windows MSVC Release + Debug and Ubuntu GCC 11 Release.
- FetchContent dependency cache keyed on FetchDependencies.cmake hash.

### Changed
- No API / ABI changes. Kernel, distance, and test contracts unchanged.

---

## [0.2.0] — 2026-04-19

**Phase 1 · Slice 02 — 距离度量（Distance Metrics）**

第二个切片。新增**欧氏距离**与**四元数测地距离**两个模块，服务于后续 KD-Tree 近邻查询与姿态空间 RBF 插值的度量需求。仍仅为纯 C++11 内核层，尚未接入 Maya。

### 新增 (Added)

- **`rbfmax/distance.hpp`**（header-only，命名空间 `rbfmax::metric`）：
  - `squared_distance(a, b)`：基于 `Eigen::MatrixBase` 模板的平方欧氏距离，KD-Tree 最近邻查询的默认度量；无 `sqrt`。
  - `distance(a, b)`：欧氏距离，通用场景。
  - `quaternion_abs_dot(q1, q2)`：反号归一后的 `|q1·q2|`，直接喂给 cosine-similarity 型核函数。
  - `quaternion_geodesic_distance(q1, q2)`：$SO(3)$ 上的测地距离 `2·acos(|q1·q2|) ∈ [0, π]`，带双分支数值保护（常规 `acos` + 近单位 `asin` 半角回退）。
- **测试套件** `tests/test_distance.cpp`：
  - 欧氏距离在 `Vector3` / `VectorX` / `Eigen::Map<>` 三种载体下零成本互通；
  - 四元数退化边界（恒等、反号、90°、180°、近单位 $\theta \approx 10^{-7}$、对称性、退化等价）；
  - **1000 组随机三元组三角不等式回归**（固定种子 `0xF5BFA1`，可复现）；
  - 500 组随机双元组 `[0, π]` 值域守护。
- **数学推导** `docs/math_derivation.md` §5：
  - 反号归一 `|q1·(-q2)| = |q1·q2|` 证明；
  - 近单位 `acos` 灾难性相消的 Taylor 展开分析与 `asin` 半角替代；
  - Lipschitz 误差上界：常规分支 `|Δd| ≲ √ε_mach ≈ 1.5e-8`，`asin` 回退分支 `|Δd| ≲ 1e-7`（双精度物理精度下限，非缺陷）；
  - 退化输入契约表（含零四元数 Debug assert 策略）。

### 修复 (Fixed)

- **TPS 契约对齐**：v0.1.0 头部注释声明 `thin_plate_spline(r<0)` 返回 NaN，但实现的 `r ≤ kLogEps` 分支早已 clamp 到 0。本次修正头部文档使其与实现一致（保留 clamp 行为，因其对"浮点抵消产生的微小负值"更鲁棒），并新增 `KernelContract.*` 三个单测锁定 Linear/Cubic/Quintic 的奇延拓、Gaussian/IMQ 的偶延拓以及 TPS 的 clamp。

### 契约 (Contracts)

- 四元数测地距离**要求**输入为单位四元数，Debug 构建以 `assert(|‖q‖² − 1| < 1e-6)` 校验；Release 信任调用方以保热路径分支最少。零四元数为未定义行为。

### 工程 (Build)

- `CMakeLists.txt` 项目版本号 `0.1.0 → 0.2.0`。
- `tests/CMakeLists.txt` 注册 `test_distance` 测试目标。

### 已知限制

- 仍**未**接入 CI 矩阵（本地 MSVC 2022 验证仍在调用方执行）。
- `quaternion.hpp`（Swing-Twist 分解、Log/Exp map）计划在 Slice 03 落盘。

---

## [0.1.0] — 2026-04-19

**Phase 1 · Slice 01 — 数学内核首切片（Kernel Functions）**

首个可验证切片。仅包含**纯 C++11 数学内核层**的核函数子模块，尚不可用于 Maya 集成。
此版本的目标是锁定跨 MSVC 14.0↔17.3 / GCC 4.8.2↔11.2.1 编译矩阵下的基础数值原语。

### 新增 (Added)

- **核函数库**（header-only）位于 `kernel/include/rbfmax/`：
  - `kernel_functions.hpp` 导出 6 种径向基核函数及其解析导数：
    - Linear (`φ(r)=r`)
    - Cubic (`φ(r)=r³`)
    - Quintic (`φ(r)=r⁵`)
    - Thin Plate Spline (`φ(r)=r²ln r`)，在 `r=0` 处经 L'Hôpital 极限解析为 `0`
    - Gaussian (`φ(r)=exp(-(εr)²)`)
    - Inverse Multiquadric (`φ(r)=(1+(εr)²)^(-1/2)`)
  - 运行时调度器 `evaluate_kernel(KernelType, r, ε)` 与 `KernelParams` 捆绑参数结构。
  - 枚举与字符串双向互转 `kernel_type_to_string` / `kernel_type_from_string`，用于后续 JSON I/O 模式稳定性。
  - 多项式尾最低次数查询 `minimum_polynomial_degree(KernelType)`。
- **类型别名**（`types.hpp`）：统一 `Scalar = double`、`Index`、Eigen 2/3/4 维向量与矩阵、动态尺寸矩阵、`Quaternion` 别名；定义 `kEps`、`kLogEps`、`kQuatIdentityEps`、`kPi` 等数值常量。

### 数学保证 (Mathematical Guarantees)

- Thin Plate Spline 在 `r ≤ 1e-30` 处强制返回 `0`，杜绝 `log(0) = -∞` 污染下游求解器。
- NaN 输入在所有核函数中**透明传播**——失败早期暴露，禁止静默吞错。
- 所有导数均为**解析式**，与中心差分在 `kFdTol = 1e-6` 容差内一致。

### 工程基建 (Tooling)

- 顶层 CMake 支持 CMake ≥ 3.14，产出 INTERFACE 目标 `rbfmax::kernel`。
- 依赖固定：Eigen 3.3.9（最高兼容 GCC 4.8.2）、GoogleTest 1.12.1（最后支持 C++11 的版本）。
- 支持离线镜像：设置 `RBFMAX_DEPS_MIRROR` 环境变量即可切换至内网 Git 镜像，适配气隙构建农场。
- 选项：`RBF_BUILD_TESTS`、`RBF_BUILD_BENCHMARKS`、`RBF_WARNINGS_AS_ERRORS`、`RBF_ENABLE_FAST_MATH`（默认关闭，避免破坏 NaN/Inf 语义）。

### 兼容矩阵（声明但尚未在 CI 中验证）

| 宿主       | Windows 编译器      | Linux 编译器   | 状态        |
|------------|---------------------|----------------|-------------|
| Maya 2018  | MSVC 14.0 (2015)    | GCC 4.8.2      | 声明支持    |
| Maya 2020  | MSVC 14.1 (2017)    | GCC 6.3.1      | 声明支持    |
| Maya 2022  | MSVC 14.2 (2019)    | GCC 9.3.1      | 声明支持    |
| Maya 2024  | MSVC 14.3 (2022)    | GCC 11.2.1     | 声明支持    |
| Maya 2025  | MSVC 14.3 (2022)    | GCC 11.2.1     | 声明支持    |

### 已知限制

- **尚未**接入 Maya SDK（阶段二目标）。
- **尚未**实现距离度量、四元数分解、求解器（阶段一后续切片）。
- **尚未**接入 CI 矩阵构建（仅本地 MSVC 2022 已验证编译路径）。

---

[Unreleased]: https://github.com/891458249/RBF_MAX/compare/v0.7.0...HEAD
[0.7.0]: https://github.com/891458249/RBF_MAX/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/891458249/RBF_MAX/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/891458249/RBF_MAX/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/891458249/RBF_MAX/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/891458249/RBF_MAX/compare/v0.2.2...v0.3.0
[0.2.2]: https://github.com/891458249/RBF_MAX/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/891458249/RBF_MAX/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/891458249/RBF_MAX/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/891458249/RBF_MAX/releases/tag/v0.1.0
