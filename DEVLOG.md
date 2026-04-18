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
