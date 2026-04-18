# Changelog

本文件记录 **对外可见** 的变更，面向使用 RBF_MAX 插件的 TD / 动画师 / 下游管线工程师。
内部重构、测试、CI、文档等变更请查阅 [`DEVLOG.md`](./DEVLOG.md)。

格式遵循 [Keep a Changelog 1.1.0](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循
[语义化版本 2.0.0](https://semver.org/lang/zh-CN/)。

---

## [Unreleased]

_尚无未发布变更。_

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

[Unreleased]: https://github.com/891458249/RBF_MAX/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/891458249/RBF_MAX/releases/tag/v0.1.0
