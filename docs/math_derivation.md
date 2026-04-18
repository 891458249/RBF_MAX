# RBF_MAX — 数学推导（阶段一：核函数）

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

## 5. 后续阶段占位

- §6 将记录四元数测地距离 $d_{\text{geo}}(q_1, q_2) = 2\arccos(|q_1\cdot q_2|)$ 的 clamp 推导（接入 `distance.hpp` 时）。
- §7 将记录 Swing-Twist 分解的纯四元数代数证明（接入 `quaternion.hpp` 时）。
- §8 将记录 Tikhonov 正则化正规方程 $(\mathbf{A}+\lambda\mathbf{I})\mathbf{w}=\mathbf{y}$ 的条件数改善估计（接入 `solver.hpp` 时）。
