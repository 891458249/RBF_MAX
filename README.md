# RBF_MAX

> 工业级 Autodesk Maya 径向基函数（Radial Basis Function）插值插件
> **当前状态**：`v0.1.0` · Phase 1 Slice 01 · 尚未可用于 Maya 集成

RBF_MAX 以纯 C++11 数学内核为基石，向上构建 Maya 并行评估节点、Viewport 2.0 实时可视化、以及 Qt6 现代化 UI，服务于 3A 级影视与游戏项目中的角色绑定（Rigging）姿态空间变形需求。

---

## 项目状态矩阵

| 阶段 | 模块 | 状态 |
|---|---|---|
| **Phase 1** Math Kernel (pure C++11) | 核函数 ✅ · 距离度量 ⏳ · 四元数 ⏳ · KD-Tree ⏳ · 求解器 ⏳ · I/O ⏳ · Benchmark ⏳ | 🚧 进行中 |
| **Phase 2** Maya DG Node | `MPxNode`, TBB, `kParallel` 调度 | 未启动 |
| **Phase 3** Viewport 2.0 Draw | `MPxDrawOverride`, `MUIDrawManager`, 热力图 | 未启动 |
| **Phase 4** Qt6 UI | PySide2/PySide6 双版本兼容，Model/View | 未启动 |

---

## 设计原则

1. **严格跨版本兼容**：Maya 2018 ↔ 2025.3，双平台 Windows + Linux。
2. **底层数学与宿主解耦**：`kernel/` 下的代码零 Maya / Qt 依赖，可直接导入 UE5 / Houdini / 纯 CLI 工具。
3. **ABI 风险清零**：TBB 强制使用 Maya 捆绑版本；禁用任何与 Maya USD 的 protobuf 冲突。
4. **NaN/Inf 透明传播**：数学错误必须早期暴露，禁止 `fast-math` 默认开启。
5. **数据驱动**：所有状态序列化为 JSON（nlohmann/json，header-only），版本化兼容。

---

## 仓库结构

```
RBF_MAX/
├── kernel/                 # 纯 C++11 数学内核（阶段一）
│   └── include/rbfmax/     # 公开头文件
├── maya/                   # Maya DG 节点与 Viewport 2.0（阶段二、三，待创建）
├── scripts/                # Python UI、MVC 控制器、UE 导出器（阶段四）
├── tests/                  # GoogleTest 单元测试
├── benchmarks/             # Google Benchmark 性能基准（阶段一尾声）
├── docs/                   # 架构与数学推导
│   └── math_derivation.md
├── cmake/                  # CMake 模块（编译器标志、依赖拉取）
├── CMakeLists.txt
├── CHANGELOG.md            # 面向用户的变更日志
├── DEVLOG.md               # 面向开发的切片级日志
├── COMMIT_CONVENTION.md    # 提交规范（Conventional Commits）
└── README.md               # 本文件
```

---

## 构建（本地验证，阶段一）

**Windows · MSVC 2022**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -- /m
ctest --test-dir build -C Release --output-on-failure
```

**Linux · GCC 11**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

**构建选项**

| 选项                     | 默认 | 说明                                           |
|--------------------------|------|------------------------------------------------|
| `RBF_BUILD_TESTS`        | ON   | 拉取 GoogleTest 1.12.1 + 编译单元测试          |
| `RBF_BUILD_BENCHMARKS`   | OFF  | 拉取 Google Benchmark 1.8.3 + 编译基准测试     |
| `RBF_WARNINGS_AS_ERRORS` | ON   | `/WX` `-Werror`                                |
| `RBF_ENABLE_FAST_MATH`   | OFF  | ⚠️ 破坏 NaN/Inf 传播语义，仅调试使用           |

**离线构建**：设置 `RBFMAX_DEPS_MIRROR=https://git.studio-internal/mirrors/` 可整体切换依赖源。

---

## 编译矩阵

| 宿主       | Windows 编译器    | Linux 编译器   | 状态            |
|------------|-------------------|----------------|-----------------|
| Maya 2018  | MSVC 14.0 (2015)  | GCC 4.8.2      | 声明支持 ⏳     |
| Maya 2020  | MSVC 14.1 (2017)  | GCC 6.3.1      | 声明支持 ⏳     |
| Maya 2022  | MSVC 14.2 (2019)  | GCC 9.3.1      | 声明支持 ⏳     |
| Maya 2024  | MSVC 14.3 (2022)  | GCC 11.2.1     | 声明支持 ⏳     |
| Maya 2025  | MSVC 14.3 (2022)  | GCC 11.2.1     | 声明支持 ⏳     |

_状态图例：✅ CI 已验证 · ⏳ 声明支持（本地单机验证） · ❌ 不支持_

---

## 许可证

_待定。内部协作阶段暂不开源。_

---

## 相关文档

- [CHANGELOG.md](./CHANGELOG.md) · 对外变更日志
- [DEVLOG.md](./DEVLOG.md) · 开发切片日志
- [COMMIT_CONVENTION.md](./COMMIT_CONVENTION.md) · 提交规范
- [docs/math_derivation.md](./docs/math_derivation.md) · 数学推导
