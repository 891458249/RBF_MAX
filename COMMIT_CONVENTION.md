# 提交规范 — RBF_MAX

本项目采用 **[Conventional Commits 1.0.0](https://www.conventionalcommits.org/zh-hans/v1.0.0/)**，配合本项目的**四阶段 + 多模块**结构制定的 scope 白名单。

---

## 1. 基本格式

```
<type>(<scope>): <subject>
<BLANK LINE>
<body>
<BLANK LINE>
<footer>
```

- **subject**：≤ 72 字符，祈使句，不以句号结尾。中英文皆可，推荐英文以便跨团队协作。
- **body**（可选）：解释「为什么」，不解释「做了什么」（diff 已说明）。换行以 `-` 开头写要点。
- **footer**（可选）：issue 引用、破坏性变更、Co-Authored-By。

---

## 2. `<type>` 枚举（固定清单，不得扩展）

| type       | 含义                                              | 是否影响版本号 |
|------------|---------------------------------------------------|----------------|
| `feat`     | 新增对外可见功能                                  | MINOR ↑        |
| `fix`      | 修复已公开的 bug                                  | PATCH ↑        |
| `perf`     | 性能改进（不改行为）                              | PATCH ↑        |
| `refactor` | 重构，无行为变化                                  | 无             |
| `test`     | 新增/修改单测、基准测试                           | 无             |
| `docs`     | 文档、注释、数学推导                              | 无             |
| `build`    | 构建脚本、CMake、依赖拉取                         | 无             |
| `ci`       | 持续集成、GitHub Actions、脚本                    | 无             |
| `chore`    | 杂项（.gitignore、格式化、许可证、发布标签等）    | 无             |
| `style`    | 纯格式化（空白、分号），不改逻辑                  | 无             |

破坏性变更在 `<type>` 后添加 `!`：`feat(kernel)!: switch Scalar to long double`，并在 footer 添加 `BREAKING CHANGE: ...`，**触发 MAJOR ↑**。

---

## 3. `<scope>` 白名单

与四阶段结构一一对应，保证 `git log --grep "(kernel)"` 能精确命中：

| scope        | 物理路径                                           | 所属阶段 |
|--------------|----------------------------------------------------|----------|
| `kernel`     | `kernel/**`                                        | 阶段一   |
| `solver`     | `kernel/include/rbfmax/solver.hpp` + 相关 `src/`   | 阶段一   |
| `quat`       | `kernel/include/rbfmax/quaternion.hpp`             | 阶段一   |
| `distance`   | `kernel/include/rbfmax/distance.hpp`               | 阶段一   |
| `kdtree`     | `kernel/include/rbfmax/kdtree.hpp`                 | 阶段一   |
| `io`         | `kernel/include/rbfmax/io_json.hpp`                | 阶段一   |
| `node`       | `maya/node/**`（阶段二创建）                       | 阶段二   |
| `draw`       | `maya/drawoverride/**`（阶段三创建）               | 阶段三   |
| `ui`         | `scripts/**`（阶段四 PySide UI）                   | 阶段四   |
| `tests`      | `tests/**`                                         | 全阶段   |
| `bench`      | `benchmarks/**`                                    | 全阶段   |
| `build`      | `CMakeLists.txt`, `cmake/**`                       | 全阶段   |
| `ci`         | `.github/**`, CI 脚本                              | 全阶段   |
| `docs`       | `docs/**`, `*.md`                                  | 全阶段   |
| `repo`       | 仓库根元数据（`.gitignore`、`.gitattributes` 等）   | 全阶段   |

scope 可省略（适用于跨越多模块的大改），但**强烈建议填写**。单次提交若命中 ≥ 3 个 scope，应拆分。

---

## 4. 常用示例

```
feat(kernel): add Gaussian kernel with analytic derivative

- Expose evaluate_kernel() dispatcher for runtime KernelType selection.
- Clamp TPS at kLogEps to prevent log(0) NaN propagation.
- Cover r=0 limits, NaN pass-through, FD derivative parity in unit tests.
```

```
fix(solver): guard Cholesky fallback against semi-definite matrices

When all driver samples collapse onto a single pose, A becomes rank-deficient
and LLT returns NumericalIssue. Fall back to LDLT with pivoting so the
evaluator still produces a usable weight vector instead of throwing.
```

```
perf(node): pre-allocate scratch workspace per evaluation thread

Refs #42
```

```
refactor(quat)!: switch Swing-Twist to right-hand-rule convention

BREAKING CHANGE: swing_axis and twist_axis outputs now follow the right-hand
rule. Assets saved before v0.4.0 must be re-baked via `rbfmax_migrate_v04.py`.
```

```
docs(kernel): add L'Hopital derivation for TPS limit at r=0
```

```
build(repo): bump Eigen pin from 3.3.7 to 3.3.9 for GCC 11 compat
```

---

## 5. 强制工程护栏

**本地 hook（待接入）**：`commit-msg` 将校验：
1. `<type>` 落在 §2 清单；
2. `<scope>` 落在 §3 白名单（缺省允许）；
3. subject ≤ 72 字符；
4. body 每行 ≤ 100 字符。

**CI 护栏（待接入阶段二）**：PR 会在 GitHub Actions 运行 `commitlint --from origin/master`，不合规直接阻塞合并。

---

## 6. 常见反例

| ❌ 反例                             | ✅ 正确                                    |
|-------------------------------------|-------------------------------------------|
| `update`                            | `fix(solver): avoid NaN in acos clamp`    |
| `wip`                               | `feat(ui): scaffold Pose Manager widget`  |
| `fix bug`                           | `fix(node): release MDataHandle on early return` |
| `feat: added a lot of things`       | 拆分为多次提交，每次一个 scope            |
| `[Phase1] kernel impl`              | `feat(kernel): implement radial kernels`  |

---

## 7. 与 CHANGELOG / DEVLOG 的关系

- **`feat` / `fix` / `perf` 以及带 `!` 的破坏性提交 → 必须同步进 `CHANGELOG.md`**（面向用户）。
- **所有切片级提交 → 必须同步进 `DEVLOG.md`**（面向开发，包含技术决策与风险）。
- `refactor` / `test` / `docs` / `build` / `ci` / `chore` / `style` 可以不进 CHANGELOG，但**必须进 DEVLOG**（至少一行摘要）。

版本号抬升时机（由 DEVLOG 记录触发）：
- 阶段一切片迭代 → `0.1.0` → `0.2.0` → `0.3.0` …（MINOR 抬升）
- 阶段一全部完工 → `1.0.0` 候选，开 pre-release 分支
- 阶段四全部完工 + 生产验证 → `1.0.0` 正式发布
