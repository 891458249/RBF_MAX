# RBF_MAX — 安装指南（终端用户）

本文件是给 **Maya 用户** 的安装入口。如果你只想把 RBF_MAX 插件装进 Maya 使用，**不要**从源码构建 —— 按下面的"终端用户"流程即可。

如果你要修改源码，参见 [`maya_node/README.md`](../maya_node/README.md)。

---

## 支持的 Maya 版本

- Maya **2022**（Windows x64）
- Maya **2025**（Windows x64）

macOS / Linux 目前**未验证**；理论上 installer 逻辑跨平台，但需要在对应系统上重新构建 `.mll` / `.so` / `.bundle`。

---

## 终端用户：两步安装

### 方案 A：使用预打包的发行包（推荐）

1. 从 Release 页下载 `RBF_MAX_installer_<version>.zip`
2. 解压到任意不含中文或空格的本地路径（如 `C:\Tools\RBF_MAX_installer\`）
3. 启动 Maya 2022 或 2025
4. **把 `drag_drop_install.py` 拖入 Maya 视窗**（3D 视窗，不是 Script Editor）
5. 在弹出的对话框里点 **"安装"**

安装完成后插件会立即加载并设置为下次 Maya 启动时自动加载。

### 方案 B：从仓库源码打包后安装（开发者场景）

如果你 clone 了本仓库并完成了 CMake 构建，跑：

```bash
python installer/package.py
```

它会把 `build-maya-2022/bin/Release/rbfmax_maya.mll` 和 `build-maya-2025/bin/Release/rbfmax_maya.mll` 复制到 `installer/plug-ins/<version>/`。之后和方案 A 一样，把 `installer/drag_drop_install.py` 拖入 Maya 视窗。

> **注意**：如果任一版本的 `.mll` 尚未构建，`package.py` 会报错退出并给出 cmake 命令指引。

---

## 卸载

再次把 `drag_drop_install.py` 拖入 Maya 视窗；检测到已安装后会提供 **"重新安装 / 卸载 / 取消"** 三选项。

卸载流程：
- 删除场景中所有 `mRBFNode` 和 `mRBFShape` 节点
- `unloadPlugin rbfmax_maya`
- 关闭自动加载
- 删除 `%USERPROFILE%/Documents/maya/modules/RBF_MAX/` 目录
- 删除 `%USERPROFILE%/Documents/maya/modules/RBF_MAX.mod` 文件

完全零残留。

---

## 验证安装

Maya Script Editor 的 Python 标签下：

```python
import maya.cmds as cmds

rbf = cmds.createNode('mRBFNode')
shp = cmds.createNode('mRBFShape')
cmds.connectAttr(rbf + '.message', shp + '.sourceNode')
# Viewport 2.0 下 mRBFShape 位置应显示 fit-center 的球体
```

---

## 更多资料

- [`installer/README.md`](../installer/README.md) — 完整的 installer 使用说明（中文）
- [`maya_node/README.md`](../maya_node/README.md) — 插件节点 + 命令用户手册
- [`README.md`](../README.md) — 项目总览
- [`DEVLOG.md`](../DEVLOG.md) — 切片级开发日志

## 故障排查速查

| 症状 | 处置 |
|---|---|
| 拖入文件没反应 | 确认拖到 3D 视窗、Script Editor 底部看错误输出 |
| "找不到 Maya XXXX 对应的插件" | 运行 `python installer/package.py` 先把 .mll 打包 |
| 重启 Maya 后插件没加载 | 检查 `%USERPROFILE%/Documents/maya/modules/RBF_MAX.mod` 是否存在 |

详细排查请看 [`installer/README.md`](../installer/README.md) 的 "故障排查" 节。
