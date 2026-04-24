# Phase 2A.5 Master Prompt — RBF_MAX Production Kernel Capability

> **Living document** — 滚动更新。用于 Phase 2A.5 内后续切片（17B-17I）执行者会话的首条消息。
> 每次切片合并后由维护者更新"进度"段；主体任务描述、基准文档、硬约束保持稳定。
>
> 起草依据：`C:\Users\Administrator\.claude\plans\claude-reactive-hennessy.md`
> （三源对比分析：铁拳 8 AnimaDriver 落地基准 / chadvernon 算法参考 / "优化提示词：Claude插件开发" 工程纪律）。
>
> **当前 Phase 2A.5 进度**（last updated 2026-04-24）：
> - ✅ Slice 17A 已合并（main HEAD `2a3aa0e`）— FeatureSpec 异构输入 + Hetero fit() 重载 + scalar-only byte-identical oracle（kernel 159/159，adapter 41/41，bench 1.44 μs）。
> - 🟢 Slice 17B 为下一目标 — 接 cmt binary parity fixture（`scripts/gen_cmt_fixture.mel` + T-30 硬门禁容差 ≤ 1e-10）。
> - 🔒 Slice 17C–17I 依序排队。
>
> 17B 执行者进场时：基准 main HEAD 应为 `2a3aa0e` 或其后续。若已推进到 17C 及以后，请把本段的"当前进度"更新后再进 Plan 模式。

---

**任务**：对 `X:\Plugins\RBF_MAX`（v1.2.0，当前 main HEAD 视"进度"段而定；Phase 2A + Phase 2B 均已收官；Phase 2A.5 分 9 切片推进中）执行本段"进度"指示的**下一切片**，把 kernel 推向"铁拳 8 AnimaDriver 级别的 production 补助骨驱动器"，为未来 Phase 2C（Qt6 UI + 6 项 roll-over）提供能打的底座。请先进入 Plan 模式产出切片化实施计划后再动手。

Phase 2B 已交付：Slice 13 (mRBFShape + DrawOverride) / Slice 14 (HM-1 viridis) / Slice 15 (HM-2 PredictionField + X-Ray) / Slice 16 (close-out + v1.2.0)。`mRBFNode` 已累计 **5 个 additive const getter**（`is_loaded` / `centers_for_viewport` / `weights` / `input_dim` / `predict_batch_samples`），R-44 不变量（mRBFShape 禁用 `MFnTypedAttribute(kString)`）永久生效。Phase 2C 有 6 项 roll-over 待消化（T-10 typeId block / T-16 `rbfmaxAttachShape` / T-18 bbox tighten / T-19 normalization UX / T-20 LUT precision / `scripts/dev_unload.py`），**本任务不涉及 2C roll-over，只做 kernel 层**。

Phase 2A.5 已交付：Slice 17A（`2a3aa0e`）— 新建 `kernel/include/rbfmax/feature_spec.hpp`（`SolverSpace` 枚举 + `QuatBlock` + `FeatureSpec`）；`solver::fit()` 追加 2 个 hetero 重载；`FitResult` 追加 4 个尾字段（`feature_spec` / `quat_features` / `feature_norms` / `distance_norm`）；`solver.cpp` 落 anon ns `build_composite_distance_matrix`（cmt steps 1–6，暂不公开）。scalar-only path 走 overlay dispatch，legacy `fit()` 函数体 0 改动，14 字段 byte-identical oracle 保护。遗留 T-30（cmt binary parity fixture）归属 17B。

### 基准文档（必读，优先级从高到低）

1. `docs/源文档/铁拳技术文档/鉄拳8で進化した補助骨とその応用例の紹介.pptx`——AnimaDriver 六节点架构、RBFSolver 的 Driver Clamp / Output Base Value / per-bone 拆分 / pose pruning 四项扩展、Maya↔game-engine 一致性方案、local Transform 分离保存。**落地基准**。
2. `docs/源文档/铁拳技术文档/『鉄拳8』リアルな筋肉表現をどう実現したか.pptx`——BendRoll / ExpMap / Quat / Euler 四种回转表达的使用场景、艺术家 pose 设定铁律、各部位补助骨数量分布。
3. `docs/源文档/铁拳技术文档/『鉄拳8』 カスタマイズキャラクターのための多重リグシステム~...pptx`——多重 rig 对 RBF 输入空间的约束，影响未来 UE 集成。
4. `docs/源文档/chadvernon/cmt-master/src/{rbfNode.h,linearRegressionSolver.{h,cpp}}`——参考实现：混合标量+多 quat 距离矩阵、`SolverSpace` 三模式、per-sample 自适应半径、θ=pinv(MᵀM+λI)Mᵀ·I 的 one-hot pose 权重形式、`averageQuaternion` 特征向量四元数加权平均。
5. `docs/源文档/chadvernon/jsRadial-master/jsRadPose.{h,cpp}` 作对比验证。
6. `docs/源文档/优化提示词：Claude插件开发.docx`——kParallel 调度纪律、MDataBlock I/O 严格性、MPxNode::internalValue 存大样本、Eigen AVX 对齐、MUserData 主/渲分离、Qt6 Model/View + 异步命令、evaluator blocker 审计（后期用）。
7. 网络：WebFetch `https://www.chadvernon.com/blog/rbf/` 与 `https://www.chadvernon.com/blog/rbf-quaternion-input/`，以及 `https://en.wikipedia.org/wiki/Cholesky_decomposition` / `https://en.wikipedia.org/wiki/Moore-Penrose_inverse` 只在需要求证数值分析细节时读。

**有冲突时以铁拳文档为准**。chadvernon 是算法参考、优化提示词是工程纪律参考，都非最终判据。

### 当前项目索引（只读）

- `README.md`（Phase 状态与设计决策）
- `kernel/include/rbfmax/{interpolator,solver,kernel_functions,distance,quaternion,kdtree,types,io_json}.hpp`
- `kernel/src/{interpolator,solver,io_json}.cpp`
- `maya_node/src/{mrbf_node,mrbf_shape,mrbf_draw_override,rbfmax_train_cmd,color_mapping,draw_sink_core,plugin_main}.cpp`
- `docs/spec/math_derivation.md`（14 章）、`docs/spec/schema_v1.md`
- `DEVLOG.md`（Slice 历史）
- `C:\Users\Administrator\.claude\projects\X--Plugins-RBF-MAX\memory\MEMORY.md` 记忆索引必须读，特别是 `phase2_reviewer_discipline.md`（5 条 Rule：grep-verify / Section G self-audit / Maya doc skepticism / 系统假设消元调试 / Autodesk canonical sample pattern 偏离检查）与 `phase2a_progress.md`（架构不变量与 Phase 2B 进入条件）。

### 已锁策略（用户在上游确认）

- **节奏**：Phase 2B 整体已收官（Slice 16 / v1.2.0）；**本任务只做 Phase 2A.5**；Phase 2C 在 2A.5 结束后启动。
- **序列化**：JSON `rbfmax/v1` 继续作 TA-readable master，**不翻 README 的 JSON 决策**；二进制 sidecar（`rbfmax/v2-binary`）延后到 Phase 3，本任务只做 schema v1→v1.1 向后兼容的非破坏性升级（新字段可选，旧 JSON 零差异回归）。
- **Maya 节点 (`mRBFNode`) 行为契约**：本任务内**禁止修改** `compute()` / `try_load()` / 已注册属性 / 已有 5 个 additive const getter 的签名与语义。若 Phase 2A.5 新增的 kernel 能力（四元数输出、envelope、base value 等）需要 Maya 层曝光，**只能继续以 additive const getter 模式追加**，不得修改现有 API。避免 2A 验收过的 bit-identical 回归风险。
- **R-44 / 承接不变量**：DEVLOG Slice 16 列出的全部 invariant 继续生效；`mRBFShape` 禁用 `MFnTypedAttribute(kString)`；Phase 2B 已有属性（`aHeatmapMode` / `aGridResolution` / `aGridExtent` / `aGridZ` / `aXRayMode` / `aSourceNode` / `aDrawEnabled` / `aSphereRadius`）禁止改动。

### Phase 2A.5 切片骨架（参考；执行者可再细化）

**Slice 17A — FeatureSpec 异构输入**
新建 `FeatureSpec{ scalar_dim, quat_blocks[N]{ SolverSpace::{Full|Swing|Twist|SwingTwist}, axis } }`；`fit()` 重载接受 `FeatureSpec` + 拆分后的训练数据；距离矩阵拼接按 chadvernon 做法（每 quat 贡献 2 列），L2 归一化按列；旧 `fit(centers, targets)` 保留作 scalar-only 的薄 wrapper。验收：新增三路 gtest，**Phase 1 既有 139 用例 + adapter 180 用例全绿零回归**。

**Slice 17B — Swing/Twist 距离接入**
`quaternion.hpp::decompose_swing_twist` 已有，直接在 solver 里调。实现数值对比 chadvernon `cmt` 固定输入，容差 ≤ 1e-10。

**Slice 17C — Driver Clamp**
`FeatureSpec` 持 per-attr envelope（scalars 的 `(min,max)` 以及 quats 的一组 pose 集合）；predict 前对标量线性钳制，对四元数做"到最近 pose 的 slerp 截断"（不能简单欧氏钳制）；`Interpolator::predict()` noexcept 契约保留。

**Slice 17D — Output Base Value**
`FitResult::base_vec`（含 scalar + quat 通道，quat 通道的 base 是单位四元数）；fit 前减、predict 后加（四元数走 `q_base * q_delta`）。schema v1.1 字段新增。

**Slice 17E — 四元数输出通道**
`TargetSpec{ scalar_cols, quat_cols }`；实现 `rbfmax::rotation::weighted_average_quaternion(MatrixX qs, VectorX w)`（Σwqqᵀ 最大特征向量，参 `cmt::averageQuaternion`）；θ 切到 N×N one-hot pose-weights（与标量输出通道共用 θ）；`predict()` 返回 `Prediction{ scalar_vec, quat_vec }`；**ScratchPool 零堆分配契约保留**——新增 `quat_scratch` 字段。

**Slice 17F — Per-sample 自适应半径**
`sampleRadius_[s] = min_{j≠s} d(s,j)`；Gaussian 与 Wendland 路径接受 per-sample 尺度；加 flag `options.adaptive_radius = false` 向后兼容。

**Slice 17G — Wendland C2 kernel**
`KernelType::kWendlandC2`：`φ(r) = (1-r/r₀)⁴·(4r/r₀+1)` for r<r₀ else 0；`minimum_polynomial_degree = -1`；紧支撑意味着可以进 kd-tree KNN（现在 kd-tree 仅 Gaussian）——本 slice 把 kd-tree 路径扩到 Wendland。

**Slice 17H — Pose pruning 工具**
`tools/prune.hpp`：`PruneReport prune_poses(FeatureSpec&, MatrixX& train, PruneOptions)`；删重复 pose、删 unconnected 属性、删所有 pose 同值属性。`rbfmaxTrainAndSave` 加 `--prune=off|safe|aggressive`。

**Slice 17I — schema v1 → v1.1 向后兼容升级**
新字段：`feature_spec`, `target_spec`, `base_values`, `envelope`, `adaptive_radius`；旧 v1 加载走 upgrader：缺字段时推断为 scalar-only / 无 envelope / 无 base / 非 adaptive；`docs/spec/schema_v1.md` 写升级章节。零差异回归：老 JSON 加载→save 后与原文本等价（忽略新增字段的空值）。

### 硬约束

- **C++11 底线**（Maya 2018 GCC 4.8.2）。
- **所有新增 API 必须 noexcept**（承接 Phase 2A 数值契约）。
- **ScratchPool 零堆分配契约**：predict 热路径（含四元数输出）不得 allocate。
- **纯 kernel 层**：本 Phase 不改 `maya_node/`（除非需要追加 additive const getter 作 kernel 新能力的曝光出口，且必须保留既有 5 个 getter 签名/语义不动）。
- **不破坏 Phase 2A bit-identical**：老 JSON + 老 API → 老输出，Maya 2022 / 2025 跨版本校验不退。
- **遵守 `phase2_reviewer_discipline.md` 五条**：Rule 1 grep-verify 所有 Phase 1 API 引用、Rule 2 dispatch 前 Section G self-audit、Rule 3 Maya 官方 API 先存疑、Rule 4 系统假设消元调试、Rule 5 偏离 Autodesk canonical sample pattern 视为 pre-dispatch 风险（Slice 13 Path A→B 经验）。

### 初始步骤

1. 读 `.remember/now.md` 与 `MEMORY.md` 全量、`phase2a_progress.md`、`phase2_reviewer_discipline.md`（5 条 Rule + 候选 Rule 6 "CI 异构 warning 审查"）、`DEVLOG.md` 自 Slice 13 起的全部条目（含 17A §A–§G）。
2. **状态确认**（应已为真；若不符请先排查）：main HEAD = 本文件 header"进度"段给出的 commit、Phase 1 ctest 159/159（17A 后）、adapter ctest 200/200、`build-maya-2022` / `build-maya-2025` 各 4 smoke 全绿、plugin 版本字符串 `"1.2.0"`（v1.3.0 延到 Phase 2A.5 close-out slice 做）。
3. 读本提示词中基准文档 #1 #4 #6（其余按需）。
4. 用 Explore agent 一次跑：`kernel/include/rbfmax/*.hpp` 的公共 API 与不变量、`tests/test_solver.cpp` + `tests/test_interpolator.cpp` 的覆盖形态、`rbfmaxTrainAndSave` 的训练数据流、`mRBFNode` 已有的 5 个 additive const getter 的实现（`is_loaded` / `centers_for_viewport` / `weights` / `input_dim` / `predict_batch_samples`）以避免 Slice 17 系列破坏 additive 契约。
5. 进 Plan 模式，产出 Slice 17A~17I 的切片计划，粒度沿 DEVLOG 里 Slice 10~16 的风格：进入条件 / 验收标准 / 预计 LOC / 风险 / 回退路径。每切片独立 PR，独立 DEVLOG 条目。
6. 暂不写代码，等 ExitPlanMode 审批。

### 回归验证（2A.5 完成后）

```bash
# 1. 既有测试全绿
cmake --build build --config Release
ctest --test-dir build --output-on-failure
# 期望：Phase 1 139 baseline + 已合并切片新增 + 当前切片新增单元用例全 PASS
#   · 17A 合并后 kernel 基线 = 159；后续切片各自追加
#   · 基数以 main DEVLOG 最新切片条目 "§F 文件变更" 里给的 "Test count delta" 为准

cmake --build build-adapter --config Release
ctest --test-dir build-adapter --output-on-failure
# 期望：adapter 41 tests 不回归；aggregate = kernel + adapter

# 2. 跨 Maya 版本 bit-identical 回归
cmake --build build-maya-2022 --config Release
cmake --build build-maya-2025 --config Release
# 比对 rbfmaxTrainAndSave 产出 JSON 的 hash；应一致

# 3. 基准不回退
cmake --build build-bench -j
./build-bench/bin/benchmarks/bench_solver
# predict N=1000 hotpath 仍 ≤ 5μs（allocation-free）
```

Phase 2A.5 完成后方可进入 Phase 2C。
