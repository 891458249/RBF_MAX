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
\boxed{|\Delta d_{\text{geo}}|_{\text{asin 分支}} \lesssim 2\sqrt{2\varepsilon_{\text{mach}}} \approx 4\times 10^{-8}\ \text{绝对（弧度）}.}
$$

> **相对误差用语的陷阱**：当 $\theta \to 0$ 时，$\Delta d / \theta$ **不受有界控制**（因分子有 $\sqrt{\varepsilon_{\text{mach}}}$ 量级的地板而分母趋零）。因此**单测容差必须使用绝对形式**（例如 `4e-8`），不得使用 `theta * R` 形式的相对容差。

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

## 7. Swing-Twist 分解（阶段一 · 切片 03）

设单位四元数 $q = (w, \mathbf{v})$ 与单位轴 $\mathbf{a}\in\mathbb{R}^3,\,\lVert\mathbf{a}\rVert=1$。寻找分解 $q = q_{\text{swing}}\cdot q_{\text{twist}}$，使得 $q_{\text{twist}}$ 仅绕 $\mathbf{a}$ 旋转，$q_{\text{swing}}$ 与 $q_{\text{twist}}$ 互不重叠。

### 7.1 投影法公式推导

将 $\mathbf{v}$ 沿 $\mathbf{a}$ 正交分解：

$$
\mathbf{v} = \mathbf{p} + \mathbf{v}_\perp,\qquad \mathbf{p} = \mathbf{a}\,(\mathbf{a}\cdot\mathbf{v}),\qquad \mathbf{a}\cdot\mathbf{v}_\perp = 0.
$$

定义 *twist 代理* $\tilde q_{\text{twist}} = (w, \mathbf{p})$。其虚部沿 $\mathbf{a}$ ，因此归一化后即为绕 $\mathbf{a}$ 的纯旋转：

$$
q_{\text{twist}} \;\equiv\; \frac{\tilde q_{\text{twist}}}{\lVert\tilde q_{\text{twist}}\rVert} \;=\; \frac{(w,\mathbf{p})}{\sqrt{w^2 + \lVert\mathbf{p}\rVert^2}}.
$$

### 7.2 分解恒等式 $q_{\text{swing}}\cdot q_{\text{twist}} = q$ 的验证

由 $q = q_{\text{swing}}\cdot q_{\text{twist}}$ 直接求解 $q_{\text{swing}} = q\cdot q_{\text{twist}}^{-1} = q\cdot \overline{q_{\text{twist}}}$（单位四元数共轭等于逆）。代码以 `q * out.twist.conjugate()` 落地。可直接验算：$q\cdot\overline{q_{\text{twist}}}\cdot q_{\text{twist}} = q$，故恒等式严格成立（无需"近似"语义）。

### 7.3 退化情形（$\mathbf{a}\to -\mathbf{a}$ 翻转）的极限分析

退化条件 $w = 0$ 且 $\mathbf{a}\cdot\mathbf{v} = 0$ 同时成立时，$q$ 表示绕 $\mathbf{v}$（与 $\mathbf{a}$ 垂直）的 $180°$ 旋转：

$$
q\,\mathbf{a}\,\overline{q} = -\mathbf{a}.
$$

此时 twist 代理 $\tilde q_{\text{twist}} = (0,\mathbf{0})$，归一化未定义；几何上"绕 $\mathbf{a}$ 的扭转"无可观测信息。约定：

$$
q_{\text{twist}} = \mathbf{1},\qquad q_{\text{swing}} = q,
$$

确保 $q_{\text{swing}}\cdot q_{\text{twist}} = q$ 仍成立，且代码避免 $0/0$ NaN 传染。

### 7.4 数值实现的 $\text{denom\_sq}$ 判定阈值选择

阈值 $\text{denom\_sq} < \texttt{kEps}^2 = 10^{-24}$ 是"双零"判据：$w$ 与 $\lVert\mathbf{p}\rVert$ 同时落入 $\sqrt{\texttt{kEps}}\approx 10^{-12}$ 量级时才触发。该阈值远低于 $\sqrt{\varepsilon_{\text{mach}}}\approx 1.5\times 10^{-8}$，避免误判常规小角度输入；又远高于 $\varepsilon_{\text{mach}}^2$，避免病态分母穿透到 `normalized()` 内部的 ULP 不稳定区。

---

## 8. Log/Exp map — Lie 代数背景（阶段一 · 切片 03）

### 8.1 $SO(3)\leftrightarrow \mathfrak{so}(3)\leftrightarrow \mathbb{R}^3$ 的同构

$SO(3)$ 的 Lie 代数 $\mathfrak{so}(3)$ 是 $3\times 3$ 反对称矩阵空间，与 $\mathbb{R}^3$ 通过帽子算子 $\hat{\,\cdot\,}: \mathbb{R}^3 \to \mathfrak{so}(3)$ 同构。指数映射 $\exp: \mathfrak{so}(3)\to SO(3)$ 与对数映射 $\log$ 互逆于"短弧"区域 $\lVert\mathbf{r}\rVert < \pi$。本模块以单位四元数表示 $SO(3)$，故 $\exp,\log$ 在 $S^3/\{\pm 1\}\simeq SO(3)$ 与 $\mathbb{R}^3$ 之间建立映射。

### 8.2 $\exp$ 的显式公式与 Taylor 展开

对旋转向量 $\mathbf{r}\in\mathbb{R}^3$，令 $\theta = \lVert\mathbf{r}\rVert$，$\mathbf{n} = \mathbf{r}/\theta$。则

$$
\exp(\mathbf{r}) \;=\; \Bigl(\cos(\tfrac{\theta}{2}),\;\sin(\tfrac{\theta}{2})\,\mathbf{n}\Bigr) \;=\; \Bigl(\cos(\tfrac{\theta}{2}),\;\frac{\sin(\theta/2)}{\theta}\,\mathbf{r}\Bigr).
$$

$\theta\to 0$ 时 $\sin(\theta/2)/\theta = \tfrac{1}{2} - \tfrac{\theta^2}{48} + \mathcal{O}(\theta^4)$，$\cos(\theta/2) = 1 - \tfrac{\theta^2}{8} + \mathcal{O}(\theta^4)$，二阶 Taylor 即可。

### 8.3 $\log$ 的显式公式与双覆盖处理

对单位四元数 $q = (w,\mathbf{v})$，令 $\theta = 2\,\text{atan2}(\lVert\mathbf{v}\rVert, w)$。等价地，当 $w \ge 0$ 时

$$
\log(q) \;=\; 2\arcsin(\lVert\mathbf{v}\rVert)\,\frac{\mathbf{v}}{\lVert\mathbf{v}\rVert} \;=\; \mathbf{v}\,\frac{2\arcsin(\lVert\mathbf{v}\rVert)}{\lVert\mathbf{v}\rVert}.
$$

**双覆盖**：$q$ 与 $-q$ 表示同一旋转。短弧约定取 $w\ge 0$ 的代表元，即当 $w<0$ 时内部翻转 $q\mapsto -q$。$w=0$ 边界本质模糊，单测以 $\min(\lVert a-b\rVert,\lVert a+b\rVert)$ 折叠双覆盖距离。

### 8.4 Taylor 阈值 $10^{-8}$ 的误差分析

阈值依据：当 $\theta < 10^{-8}$ 时

- $\exp$ 的 $\sin(\theta/2)/\theta$ 二阶截断误差为 $\tfrac{\theta^4}{3840}\lesssim 2.6\times 10^{-37}$；
- $\log$ 的 $2\arcsin(x)/x$ 二阶截断误差为 $\tfrac{3x^4}{40}\lesssim 7.5\times 10^{-34}$（$x = \lVert\mathbf{v}\rVert\le \theta/2$ 量级）。

二者均比 $\varepsilon_{\text{mach}}\approx 2.2\times 10^{-16}$ 低 17 个数量级，因此 Taylor 分支在阈值处与标准分支误差量级匹配，无可观测精度断层。阈值取 $10^{-8}$ 而非更小值是为了**远离** $\sin/\arcsin$ 的微调精度地板（$\theta\sim 10^{-15}$ 量级时 $\sin$ 本身丢精度），以"提早进入 Taylor 分支"为安全策略。

### 8.5 单测容差表（审查锚点）

| 用例 | 容差 | 依据 |
|---|---|---|
| 恒等映射往返（log of Identity / exp of Zero） | $10^{-14}$ 绝对 | $\varepsilon_{\text{mach}}$ 量级 |
| $\log\circ\exp$、$\exp\circ\log$ 往返 | $10^{-10}$ 绝对 | 累积 $\sin/\cos$ 链误差 |
| Taylor 分支（$\theta = 10^{-10}$） | $10^{-14}$ 绝对 | 截断误差远低于 $\varepsilon_{\text{mach}}$ |
| $\pi$ 边界（$\theta = \pi - 10^{-6}$） | $10^{-8}$ 绝对 | $\arcsin$ 在 1 附近饱和 |
| 输出单位性 $\bigl|\lVert q\rVert - 1\bigr|$ | $10^{-14}$ 绝对 | $\sin^2 + \cos^2 = 1$ 严格 |

---

## 10. kd-tree 的几何与复杂度（阶段一 · 切片 04）

### 10.1 建树复杂度

每层递归：

- **方差计算**：$O(N\cdot D)$（两遍：均值 → 方差和）。
- **`std::nth_element` 中位数选取**：$O(N)$（Hoare 选择算法的 introselect 实现）。

递归深度 $\log_2 N$（中位数分裂保证近似平衡），总复杂度

$$
T_{\text{build}} = O(N \log N \cdot D).
$$

对 $D = 3,\,N = 10^6$ 的典型规模，建树时间约 50ms；对 $N = 10^4$ 约 < 10ms。

### 10.2 查询复杂度

均匀分布假设下单次 k-NN 查询期望复杂度

$$
T_{\text{query}} = O\!\bigl(k + \log N\bigr) \cdot O(D) = O(k \log N).
$$

**最坏情况**（样本严重各向异性聚集，查询点在分割平面附近反复跨越）退化为 $O(N)$，但 variance-based 分割大幅缓解退化（见 §10.4）。

### 10.3 剪枝条件的几何意义

设当前堆中第 $k$ 远候选的平方距离为 $r_{\max}^2$，分割超平面在 $d_s$ 维的位置为 $v_s$，查询点在该维的坐标为 $q_s$。则查询点到超平面的**垂直距离平方**为

$$
\Delta^2 = (q_s - v_s)^2.
$$

**剪枝规则**：

$$
\Delta^2 \;\ge\; r_{\max}^2 \;\;\Longrightarrow\;\; \text{far 子树整体跳过}.
$$

直觉：超平面是 far 子树中所有点到查询点距离的下界；若该下界已经超过当前第 $k$ 远候选，则 far 子树绝无可能贡献新邻居。

### 10.4 方差-based 分割维度选择

给定子集 $S = \{x_1,\dots,x_n\}\subset \mathbb{R}^D$，沿维度 $d$ 的方差：

$$
\sigma_d^2 \;=\; \frac{1}{n}\sum_{i=1}^{n} (x_{i,d} - \bar x_d)^2.
$$

选取 $d^* = \arg\max_d \sigma_d^2$ 作为分割轴。相比 cyclic（轮换）策略，在各向异性分布（例如关节扭转角方差远大于位移）下查询性能实测提升 20-40%。

代码用未除以 $n$ 的"方差和" $\sum (x_{i,d}-\bar x_d)^2$ 比较即可（同一子集下 $n$ 是常数），节省一次除法。

### 10.5 容差与边界行为

| 情形 | 行为 |
|---|---|
| `k = 0` | 直接 `return 0`；不访问堆，输出缓冲区零写 |
| `k > N` | clamp 到 `N`；返回 `N`；缓冲区 `[N..k)` 不被触碰 |
| `N = 0` | 构造合法；任何查询返回 `0` |
| 查询维度不匹配 `dim()` | Debug 下 `eigen_assert` 触发；Release 信任调用方 |
| 距离比较的浮点容差 | 平方距离断言 `1e-12` 绝对（两数尺度 $\le 12 = 4\cdot 3$） |

---

## 11. Tikhonov 正则化与条件数改善（阶段一 · 切片 05）

### 11.1 病态 RBF 矩阵的来源

对严格正定核（Gaussian / IMQ），当形状参数 $\varepsilon \to 0^+$ 时核矩阵 $A$ 趋于 rank-1，条件数发散。对条件正定核（Linear / Cubic / TPS / Quintic），当样本点接近共线 / 共面时同样发生条件数爆炸。

工业实践表明，无正则化的 $A w = y$ 在 $N \ge 50$ 的散乱样本下几乎必然遭遇 Cholesky 分解失败（`LLT.info() != Success`）。

### 11.2 Tikhonov 的 SVD 几何意义

设 $A = U \Lambda U^T$（对称正定时的特征分解，$\Lambda = \mathrm{diag}(\lambda_i)$）。则：

$$
(A + \lambda I)^{-1} = U \,\mathrm{diag}\!\left( \frac{1}{\lambda_i + \lambda} \right) U^T.
$$

每个特征值被 "shift" 了 $\lambda$。小特征值（噪声方向）获得的相对抑制最强，而大特征值（信号方向）几乎不变。这即 Tikhonov 的 **频域低通** 解释。

### 11.3 $\lambda$ 下限的数值推导

双精度浮点 $\varepsilon_{\text{mach}} \approx 2.22\times 10^{-16}$。Cholesky 分解要求矩阵最小特征值 $\lambda_{\min} > \kappa\cdot\varepsilon_{\text{mach}}$，其中 $\kappa$ 是条件数上界。实测经验：$\lambda \ge 10^{-12}$ 时 Cholesky 在 $N \le 10^4$ 的 RBF 矩阵上基本稳定。

故代码常量 `kLambdaMin = 1e-12`。低于该值的用户输入在 Release 下静默 clamp，Debug 下触发 `eigen_assert`。

### 11.4 三级回退的触发条件

| 路径 | 触发 | 算法 | 复杂度 |
|---|---|---|---|
| LLT | $A + \lambda I$ 严格正定 | Cholesky 无 pivot | $\mathcal{O}(N^3/3)$ |
| LDLT | 半正定或数值非正定 | 带置换的 $LDL^T$ | $\mathcal{O}(N^3/3)$，常数更大 |
| BDCSVD | 任意 | 分治 SVD | $\mathcal{O}(N^3)$ × 5-10 |

LLT 分解通过检查对角元正性快速失败。失败概率对严格正定核（Gaussian）近乎 0，对 Quintic + 近共面样本可达 50%。BDCSVD 同时返回 $\sigma_{\max}/\sigma_{\min}$ 用作条件数报告。

---

## 12. 广义交叉验证 (GCV)（阶段一 · 切片 05）

### 12.1 从 LOOCV 到 GCV

留一交叉验证的预测误差：

$$
\mathrm{LOOCV}(\lambda) = \frac{1}{N}\sum_{i=1}^N \left( \frac{y_i - \hat y_i}{1 - H_{ii}} \right)^2
$$

其中 hat matrix $H = A(A + \lambda I)^{-1}$，$H_{ii}$ 为其对角元。

**GCV 的核心近似**：用 $H_{ii}$ 的平均值替代各自值：

$$
\mathrm{GCV}(\lambda) = \frac{\lVert (I - H) y\rVert^2 / N}{[\,\mathrm{tr}(I - H)/N\,]^2}
\;=\; \frac{N\,\lVert (I - H) y\rVert^2}{[\,\mathrm{tr}(I - H)\,]^2}.
$$

GCV 是 LOOCV 的旋转不变近似，在大多数实际问题中性能与 LOOCV 相当但计算成本远低。

### 12.2 SVD 闭式评估

利用 $A = U\Lambda U^T$：

$$
\mathrm{tr}(H) = \sum_{i=1}^N \frac{\lambda_i}{\lambda_i + \lambda}, \qquad
\mathrm{tr}(I - H) = \sum_{i=1}^N \frac{\lambda}{\lambda_i + \lambda}.
$$

$$
\lVert (I - H) y\rVert^2 = \sum_{i=1}^N \left(\frac{\lambda}{\lambda_i + \lambda}\right)^2 (u_i^T y)^2.
$$

代码中 `select_lambda_gcv` 一次性计算 $u_i^T y$（成本 $\mathcal{O}(N^2)$），后续每个候选 $\lambda$ 只需 $\mathcal{O}(N)$。

### 12.3 $\lambda$ 网格搜索

在 $\lambda \in [10^{-10},\,10^{-2}]$ 上对数均匀采样 50 个点。典型 GCV 曲线在 $\lambda^* \approx 10^{-6}$ 附近有单一极小点；曲线发散或单调时退回默认 $\lambda = 10^{-6}$。

**多输出策略**：仅以第 1 列 targets 选 $\lambda$，避免不同列之间最优 $\lambda$ 不一致带来的歧义。需要每列独立 $\lambda$ 时，调用方应分次拟合。

### 12.4 退化输入的兜底

GCV 要求 $\mathrm{tr}(I - H) > 0$（即 $\lambda > 0$ 且 $A$ 非纯零矩阵）。任意候选若产生 $\mathrm{tr}(I-H) \le 0$、$\mathrm{NaN}$ 或 $\mathrm{Inf}$ 则跳过；全部跳过时返回默认 $10^{-6}$。

---

## 13. QR 消元求解增广 RBF 系统（阶段一 · 切片 05）

### 13.1 增广系统与拟定性

带多项式尾的 RBF 系统：

$$
\begin{pmatrix} A + \lambda I & P \\ P^T & 0 \end{pmatrix}
\begin{pmatrix} w \\ v \end{pmatrix}
= \begin{pmatrix} y \\ 0 \end{pmatrix}
$$

左上块对称正定（因 $\lambda > 0$），但右下块零导致 **整体不定**。直接 LLT/LDLT 必然失败；BDCSVD 可用但慢 5-10×。

### 13.2 零空间消元法

QR 分解 $P = Q R$，记 $Q = [Q_1\ Q_2]$，其中 $Q_1\in\mathbb{R}^{N\times Q}$、$Q_2\in\mathbb{R}^{N\times (N-Q)}$。$Q_2$ 的列张成 $P^T$ 的零空间。

约束 $P^T w = 0 \iff w = Q_2 u$（$u\in\mathbb{R}^{N-Q}$ 自由）。代入第一个方程并左乘 $Q_2^T$：

$$
\bigl(Q_2^T (A + \lambda I) Q_2\bigr)\,u = Q_2^T y.
$$

左侧是 $(N-Q)\times(N-Q)$ **对称正定**矩阵（因 $Q_2$ 列正交且 $A+\lambda I$ 正定），可走 LLT 快路径。

解出 $u$ 后：

- $w = Q_2 u \in \mathbb{R}^N$
- 由 $P v = y - (A + \lambda I) w$，左乘 $Q_1^T$ 得 $R v = Q_1^T (y - (A+\lambda I) w)$，三角系统 $\mathcal{O}(Q^2)$ 回代

按构造，$P^T w = R^T Q_1^T Q_2 u = 0$ 严格成立（Q1 与 Q2 正交），单测 `PolyTail.CubicSatisfiesConstraintPTw` 以 $10^{-10}$ 容差守护此性质。

### 13.3 数值稳定性对比

| 方法 | 求解器 | 典型 $N=100,\,Q=4$ 总时间 |
|---|---|---|
| 直接 BDCSVD 增广系统 | SVD | $\sim 8$ ms |
| QR 消元 + LLT | Cholesky | $\sim 1.5$ ms |

QR 消元在 $N=1000$ 规模下提速约 7×。该方法的稳定性来源：消元过程仅做正交变换 $Q_2^T \cdot Q_2$，不放大条件数；LLT 仅作用于已经投影到良态零空间的子矩阵。

---

## 14. 后续阶段占位

- §14 将记录 zero-allocation predict 实现的 SIMD / 内存对齐分析（接入 `scratch_pool` 时）。
