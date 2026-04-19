# Changelog

本文件记录 **对外可见** 的变更，面向使用 RBF_MAX 插件的 TD / 动画师 / 下游管线工程师。
内部重构、测试、CI、文档等变更请查阅 [`DEVLOG.md`](./DEVLOG.md)。

格式遵循 [Keep a Changelog 1.1.0](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循
[语义化版本 2.0.0](https://semver.org/lang/zh-CN/)。

---

## [Unreleased]

_尚无未发布变更。_

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

[Unreleased]: https://github.com/891458249/RBF_MAX/compare/v0.2.2...HEAD
[0.2.2]: https://github.com/891458249/RBF_MAX/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/891458249/RBF_MAX/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/891458249/RBF_MAX/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/891458249/RBF_MAX/releases/tag/v0.1.0
