# RBF_MAX — 数学推导（阶段一）

> 本文件锁定 `kernel/include/rbfmax/kernel_functions.hpp` 中每个核函数的解析形式、定义域、导数以及在 $r=0$ 处的极限行为。后续阶段新增核函数或求解器时，须在此文件追加对应章节，不得删改既有章节。

---

## 1. 径向基函数基本框架

给定一组散乱中心点 $\{\mathbf{c}_j\}_{j=1}^{N}\subset\mathbb{R}^d$ 与对应目标值 $\{y_j\}$，径向基插值寻找函数

$$
s(\mathbf{x}) \;=\; \sum_{j=1}^{N} w_j \,\varphi\!\bigl(\lVert\mathbf{x}-\mathbf{c}_j\rVert\bigr) \;+\; \sum_{\alpha=1}^{Q} v_\alpha\, P_\alpha(\mathbf{x})
$$

使得 $s(\mathbf{c}_i) = y_i$。其中 $\varphi:\mathbb{R}_{\ge 0}\to\mathbb{R}$ 为径向基核，$\{P_\alpha\}$ 为次数不高于 $m$ 的多项式基底，$m$ 由核函数的**条件正定阶数**决定。

| 核函数 | $\varphi(r)$ | 条件正定阶 $m$ | 多项式尾最低次数 | 备注 |
|---|---|---|---|---|
| Linear              | $r$                       | $m=1$ | $\deg P \ge 0$ | 三维薄板 Green 函数 |
| Cubic               | $r^3$                     | $m=2$ | $\deg P \ge 1$ | |
| Quintic             | $r^5$                     | $m=3$ | $\deg P \ge 2$ | |
| Thin-Plate Spline   | $r^2\ln r$                | $m=2$ | $\deg P \ge 1$ | 2D 双调和 Green 函数 |
| Gaussian            | $\exp\!\bigl(-(\varepsilon r)^2\bigr)$ | 严格正定 | 无需 | 需形状参数 $\varepsilon>0$ |
| Inverse Multiquadric| $\bigl(1+(\varepsilon r)^2\bigr)^{-1/2}$ | 严格正定 | 无需 | 需形状参数 $\varepsilon>0$ |

代码常量 `minimum_polynomial_degree(k)` 返回表中 "多项式尾最低次数" 一列；Gaussian / IMQ 返回 $-1$ 表示不需要多项式尾。

---

## 2. 核函数及其导数

### 2.1 Linear

$$
\varphi(r) = r, \qquad \varphi'(r) = 1 .
$$

定义域 $r\ge 0$，$C^0$ 连续，$r=0$ 处不可微（但单侧极限 $\varphi'(0^+)=1$ 连续）。

### 2.2 Cubic

$$
\varphi(r) = r^3, \qquad \varphi'(r) = 3r^2 .
$$

$C^2$ 连续于整个 $\mathbb{R}_{\ge 0}$。

### 2.3 Quintic

$$
\varphi(r) = r^5, \qquad \varphi'(r) = 5r^4 .
$$

$C^4$ 连续。适合需要极高阶光滑度的形变过渡。

### 2.4 Thin-Plate Spline（薄板样条）

$$
\varphi(r) = r^2 \ln r , \qquad \varphi'(r) = r\bigl(2\ln r + 1\bigr) .
$$

**$r=0$ 处的极限**：直接计算将给出 $0\cdot(-\infty)$ 的不定型，需求极限

$$
\lim_{r\to 0^+} r^2 \ln r
= \lim_{r\to 0^+} \frac{\ln r}{r^{-2}}
\stackrel{\text{L'H}}{=}\lim_{r\to 0^+} \frac{1/r}{-2r^{-3}}
= \lim_{r\to 0^+} -\tfrac{r^2}{2}
= 0 .
$$

同理 $\displaystyle\lim_{r\to 0^+}\varphi'(r) = \lim_{r\to 0^+} r(2\ln r + 1) = 0$。

**代码实现约定**：当 $r \le \texttt{kLogEps} = 10^{-30}$ 时，`thin_plate_spline(r)` 与 `thin_plate_spline_derivative(r)` 均直接返回 $0$，以规避 `std::log(0) = -\infty` 与 $0\cdot(-\infty) = \text{NaN}$ 的传染性。NaN 输入仍透明穿透，不被静默吞掉（单测 `ThinPlateSplineLimitAtZero` 与 `AllKernelsPropagateNaN` 同时守护这两条语义）。

### 2.5 Gaussian

$$
\varphi(r;\,\varepsilon) = \exp\!\bigl(-(\varepsilon r)^2\bigr), \qquad
\varphi'(r;\,\varepsilon) = -2\varepsilon^2\, r\,\exp\!\bigl(-(\varepsilon r)^2\bigr) .
$$

$C^\infty$，严格正定。$\varphi(0)=1$，$\lim_{r\to\infty}\varphi = 0$。

**形状参数敏感性**：定义条件数 $\kappa(\mathbf{A}_\varepsilon)$ 为插值矩阵在形状参数 $\varepsilon$ 下的 2-范数条件数。已知

$$
\lim_{\varepsilon\to 0^+} \kappa(\mathbf{A}_\varepsilon) = +\infty ,
$$

这即 RBF 的**不确定性原理**（Schaback 1995）：平滑度的提升必然伴随数值稳定性的下降。后续 `solver.hpp` 将以 Tikhonov 正则化项 $\lambda I$ 作为防御。

### 2.6 Inverse Multiquadric（IMQ）

$$
\varphi(r;\,\varepsilon) = \bigl(1+(\varepsilon r)^2\bigr)^{-1/2}, \qquad
\varphi'(r;\,\varepsilon) = -\varepsilon^2\, r \,\bigl(1+(\varepsilon r)^2\bigr)^{-3/2} .
$$

$C^\infty$，严格正定。$\varphi(0)=1$，$\lim_{r\to\infty}\varphi = 0$（长尾衰减）。

**数值实现要点**：导数公式若展开为 $\varphi^3(r)\cdot(-\varepsilon^2 r)$ 会累积三次乘法误差；代码中改为

```cpp
den = 1 + (eps*r)^2;
return -eps*eps * r / (den * sqrt(den));
```

只做一次 `sqrt` 与一次除法，在 $\varepsilon r$ 较大时（尾部区域）误差降低约 2 个数量级。

---

## 3. NaN/Inf 传播约定

| 输入 | Linear/Cubic/Quintic | TPS | Gaussian | IMQ |
|---|---|---|---|---|
| `NaN`                        | NaN              | NaN             | NaN               | NaN                |
| $+\infty$（$\varepsilon>0$） | $+\infty$        | $+\infty$       | $0$               | $0$                |
| $-\infty$（不应发生）         | $-\infty$ / $-\infty$ / $-\infty$ | NaN | $0$ | $0$ |
| $r < \texttt{kLogEps}$（TPS）| —                 | **强制 $0$**    | —                 | —                  |

测试 `AllKernelsPropagateNaN` 与 `GaussianDecaysToZeroAtInfinity` 作为回归护栏。

---

## 4. 验证策略（与单测一一对应）

| 条款 | 单测 |
|---|---|
| 解析值正确性 | `KernelValues.*` |
| 极限 $r=0$（TPS） | `KernelValues.ThinPlateSplineLimitAtZero` |
| 解析导数 ↔ 有限差分（中心差分步长 $h=10^{-6}$）| `KernelDerivatives.*AgreesWithFD` |
| NaN 透明传播 | `KernelNaN.AllKernelsPropagateNaN` |
| $\infty$ 衰减 | `KernelInfinity.GaussianDecaysToZeroAtInfinity` |
| 枚举 ↔ 字符串往返 | `KernelTypeStrings.EveryEnumValueRoundTrips` |
| 多项式尾次数表 | `KernelMetadata.MinimumPolynomialDegreeTableMatchesDocument` |

有限差分容差 $10^{-6}$ 的推导：

$$
E_{\text{trunc}}(h) = \tfrac{h^2}{6}\,\bigl|\varphi'''(\xi)\bigr|, \qquad
E_{\text{round}}(h) = \mathcal{O}\!\bigl(\epsilon_{\text{mach}}/h\bigr) .
$$

对 $\varepsilon_{\text{mach}}\approx 2.2\times 10^{-16}$，取 $h=10^{-6}$ 令两类误差同量级 $\sim 10^{-10}$；再叠加 $\varphi'''$ 在测试点上的上界 $\mathcal{O}(10^3)$（Gaussian 在 $\varepsilon r\approx 1$ 附近），总误差上界约 $10^{-7}$，故阈值 `kFdTol = 1e-6` 留有一个数量级安全裕度。

---

## 5. 距离度量（阶段一 · 切片 02）

### 5.1 欧氏距离

对 $\mathbf{a}, \mathbf{b} \in \mathbb{R}^d$：

$$
d_{\text{sq}}(\mathbf{a}, \mathbf{b}) = \lVert \mathbf{a} - \mathbf{b}\rVert^2,
\qquad
d(\mathbf{a}, \mathbf{b}) = \sqrt{d_{\text{sq}}(\mathbf{a}, \mathbf{b})} .
$$

KD-Tree NN 查询使用 $d_{\text{sq}}$（单调等价于 $d$，但省去 `sqrt`）。接口以 `Eigen::MatrixBase<Derived>` 模板接收，支持 `Vector3`、`VectorX`、`Map<>`、`Block<>` 等所有 Eigen 表达式，且不触发中间临时量的堆分配。

### 5.2 四元数测地距离 — 商空间 $S^3/\{\pm 1\}\simeq SO(3)$

#### 5.2.1 定义

对单位四元数 $q_1, q_2 \in S^3$：

$$
d_{\text{geo}}(q_1, q_2) = 2\arccos\!\bigl(|\,q_1\!\cdot\!q_2\,|\bigr) \in [0, \pi] .
$$

**反号归一（antipodal reduction）**：因 $q$ 与 $-q$ 表示相同旋转，取 $|q_1\cdot q_2|$ 将商空间 $S^3/\{\pm 1\}$ 上的距离化为 $S^3$ 上的"短弧"：

$$
|q_1\cdot(-q_2)| = |-q_1\cdot q_2| = |q_1\cdot q_2| .
$$

#### 5.2.2 数值分支判据

直接计算 $\arccos(d)$ 在 $d \to 1^-$ 区域会丢失 7-8 位有效数字（"灾难性相消"）。原因：Taylor 展开

$$
\arccos(d) = \sqrt{2(1 - d)}\,\Bigl[1 + \tfrac{1 - d}{12} + \mathcal{O}\bigl((1-d)^2\bigr)\Bigr],
$$

浮点计算 $1 - d$ 时若 $d = 1 - \varepsilon$ 且 $\varepsilon \lesssim \varepsilon_{\text{mach}}$，则 $1-d$ 在有限精度下归零。改用半角形式

$$
\arccos(d) = \arcsin\!\bigl(\sqrt{1 - d^2}\bigr)
= \arcsin\!\bigl(\sqrt{(1-d)(1+d)}\bigr),
$$

其中 $1 + d \in [1, 2]$ 精度保持完整，损失只来自 $1 - d$ 单次减法（比 `acos` 少一次转函数调用链上的放大）。

代码中的阈值 `kQuatIdentityEps = 1e-14` 即 $d \ge 1 - 10^{-14}$ 切换到 asin 分支。

#### 5.2.3 误差上界

**常规 acos 分支**（$|d| \le 1 - 10^{-14}$）：$\arccos$ 的 Lipschitz 常数在该区域为

$$
\bigl|\tfrac{d}{dx}\arccos(x)\bigr| = \tfrac{1}{\sqrt{1-x^2}} \le \tfrac{1}{\sqrt{1 - (1 - 10^{-14})^2}} \approx 7.07\times 10^{6},
$$

配合 $|d - \hat d| \lesssim \varepsilon_{\text{mach}} \approx 2.2\times 10^{-16}$，距离相对误差上界

$$
|\Delta d_{\text{geo}}| \lesssim 2 \cdot 7.07\times 10^{6} \cdot 2.2\times 10^{-16} \approx 3\times 10^{-9}.
$$

考虑 `dot` 累积误差（4 次乘加 → $\sim 4\varepsilon_{\text{mach}}$）及 clamp 操作带来的量级，**保守上界取 $|\Delta d_{\text{geo}}| \lesssim \sqrt{\varepsilon_{\text{mach}}} \approx 1.5\times 10^{-8}$（相对）**。

**asin 回退分支**（$d > 1 - 10^{-14}$）：$\sqrt{1 - d^2}$ 的精度由 $1 - d^2$ 的有效位数决定。当 $d = 1 - \delta$（$\delta \in [0, 10^{-14}]$）：

$$
1 - d^2 = (1-d)(1+d) \approx 2\delta, \qquad
\sqrt{1 - d^2} \approx \sqrt{2\delta} \in [0, \sqrt{2\cdot 10^{-14}}] \approx [0, 1.4\times 10^{-7}].
$$

此分支下距离 $d_{\text{geo}} \approx 2\sqrt{2\delta}$，其中 $\delta$ 本身只有 $\mathcal{O}(\varepsilon_{\text{mach}})$ 的绝对精度。故：

$$
\boxed{|\Delta d_{\text{geo}}|_{\text{asin 分支}} \lesssim 2\sqrt{2\varepsilon_{\text{mach}}} \approx 4\times 10^{-8}\ \text{绝对}, \quad 1\times 10^{-7}\ \text{相对}.}
$$

**这不是 bug，是物理上的精度下限**：近单位区域的旋转角度本身就落在 $\sqrt{\varepsilon_{\text{mach}}}$ 量级以内，任何度量都无法在纯 `double` 下分辨更小的差异。

#### 5.2.4 退化契约

| 输入                 | 期望                              | 实现策略 |
|----------------------|-----------------------------------|----------|
| $q_1 = q_2$          | $d = 0$                           | $|dot| = 1$ → asin(0) = 0 |
| $q_1 = -q_2$         | $d = 0$（同一旋转）               | 反号归一后同上 |
| $q_1 \perp q_2$（物理反向 180°）| $d = \pi$                | $|dot| = 0$ → $2\arccos(0) = \pi$，acos 分支稳定 |
| 非单位 $q_i$         | 未定义                            | Debug 构建 `assert(|‖q‖²-1| < 1e-6)`；Release 信任调用方 |
| $q_i = (0,0,0,0)$    | 未定义                            | Debug assert；Release 会产生 $|dot| = 0 \Rightarrow d = \pi/2$（无意义） |

零四元数的过滤由上游（Maya 节点 attribute ingress）完成，本层不重复校验以保持热路径无分支。

### 5.3 三角不等式（经验验证）

理论：$SO(3)$ 是黎曼流形，$d_{\text{geo}}$ 为其唯一双不变度量，天然满足 $d(a,c)\le d(a,b)+d(b,c)$（严格不等式除非 $b$ 在 $ac$ 测地线上）。

实现层面，我们以 **1000 组随机单位四元数三元组**、固定种子 `0xF5BFA1` 作数值回归护栏，允许 $10^{-12}$ 的累积浮点松弛。任何违规都指向实现 bug（单测 `TriangleInequality_1000Samples_FixedSeed`）。

---

## 6. 后续阶段占位

- §6 将记录 Swing-Twist 分解的纯四元数代数证明（接入 `quaternion.hpp` 时）。
- §7 将记录 Tikhonov 正则化正规方程 $(\mathbf{A}+\lambda\mathbf{I})\mathbf{w}=\mathbf{y}$ 的条件数改善估计（接入 `solver.hpp` 时）。
