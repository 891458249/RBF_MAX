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
