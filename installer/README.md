# RBF_MAX — Maya 安装程序

可以**拖入 Maya 视窗**的安装脚本。一键安装 / 卸载 `rbfmax_maya` 插件（包含 `mRBFNode` + `mRBFShape` + `rbfmaxTrainAndSave` 命令）。

支持版本：**Maya 2022** 和 **Maya 2025**。

---

## 给普通用户（最终发布后的使用流程）

1. 下载 `RBF_MAX_installer.zip` 或克隆本仓库后运行 `python installer/package.py`
2. 解压到任意位置（注意路径不要含中文或空格）
3. 启动 Maya
4. 把 `drag_drop_install.py` **拖入 Maya 视窗**
5. 在弹出的对话框里点 **"安装"**
6. 安装完成后，插件会自动加载，下次启动 Maya 也会自动加载

### 卸载
再次把 `drag_drop_install.py` 拖入 Maya 视窗，会检测到已安装，点 **"卸载"** 即可。卸载过程会：
- 删除场景中所有 `mRBFNode` / `mRBFShape`（防止 unloadPlugin 拒绝）
- `unloadPlugin rbfmax_maya`
- 关闭自动加载（`pluginInfo -autoload false`）
- 删除 `~/Documents/maya/modules/RBF_MAX/` 整个目录
- 删除 `~/Documents/maya/modules/RBF_MAX.mod` 描述文件

卸载后没有任何残留。

---

## 给开发者（构建 + 打包流程）

### 一次性准备

构建两个版本的插件：
```bash
# Maya 2022
cmake -S . -B build-maya-2022 -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_BUILD_TYPE=Release \
      -DRBF_BUILD_MAYA_NODE=ON \
      -DMAYA_VERSION=2022 \
      -DMAYA_DEVKIT_ROOT="C:/SDK/Maya2022/devkitBase"
cmake --build build-maya-2022 --config Release

# Maya 2025
cmake -S . -B build-maya-2025 -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_BUILD_TYPE=Release \
      -DRBF_BUILD_MAYA_NODE=ON \
      -DMAYA_VERSION=2025 \
      -DMAYA_DEVKIT_ROOT="C:/SDK/Maya2025/devkitBase"
cmake --build build-maya-2025 --config Release
```

### 打包到 installer/

```bash
python installer/package.py
```

这个脚本会把两个 `.mll` 从 `build-maya-*/bin/Release/` 复制到 `installer/plug-ins/<version>/`，产出如下结构：

```
installer/
├── drag_drop_install.py    ← 用户拖这个
├── README.md
├── package.py
├── plug-ins/
│   ├── 2022/rbfmax_maya.mll
│   └── 2025/rbfmax_maya.mll
└── scripts/                ← 预留，Phase 2C Qt UI 会放这里
```

之后把整个 `installer/` 目录压缩为 zip 分发即可。

---

## 安装位置说明

模块被安装到 Maya 的**用户模块目录**（不是系统目录，不需要管理员权限）：

| 平台   | 模块目录                                              |
|--------|-------------------------------------------------------|
| Windows| `%USERPROFILE%/Documents/maya/modules/`              |
| macOS  | `~/Library/Preferences/Autodesk/maya/modules/`        |
| Linux  | `~/maya/modules/`                                     |

安装后该目录下会新增两项：
- `RBF_MAX.mod` — 模块描述文件，告诉 Maya 从哪找插件
- `RBF_MAX/` — 目录，含 `plug-ins/<version>/rbfmax_maya.mll` + `scripts/`

Maya 下次启动时通过 `.mod` 文件自动识别模块；当前会话中脚本也会主动调用 `loadModule` + `loadPlugin` 立即生效。

---

## 验证安装

安装成功后，在 Maya Script Editor 的 Python 标签下执行：

```python
import maya.cmds as cmds

# 创建计算节点 + 可视化节点 + 连接
rbf = cmds.createNode('mRBFNode')
shp = cmds.createNode('mRBFShape')
cmds.connectAttr(rbf + '.message', shp + '.sourceNode')

# 加载训练好的 JSON
cmds.setAttr(rbf + '.jsonPath',
             'C:/path/to/rig.json', type='string')
cmds.setAttr(rbf + '.queryPoint', [0.5, 0.5], type='doubleArray')
print(cmds.getAttr(rbf + '.outputValues'))

# 确认 Viewport 2.0 里 mRBFShape 位置出现白色球体
```

命令用法参见根目录 `maya_node/README.md`。

---

## 故障排查

### 拖入文件没反应
- 确认你拖的是 **`drag_drop_install.py`**，不是其它文件
- 确认拖到的是 **Maya 的 3D 视窗**（不是 Script Editor 或 Outliner）
- 查看 Script Editor 底部，拖放错误会打印在那里

### 安装时报"找不到 Maya XXXX 对应的插件"
- 确认 `installer/plug-ins/<your_version>/rbfmax_maya.mll` 存在
- 如果是克隆仓库后直接拖，要先跑 `python installer/package.py`
- 如果你的 Maya 是 2022 / 2025 之外的版本（如 2024），目前不支持

### 安装后重启 Maya 插件没自动加载
- 检查 `%USERPROFILE%/Documents/maya/modules/RBF_MAX.mod` 是否存在
- 在 Maya 里运行 `cmds.moduleInfo(listModules=True)`，看输出里是否有 `RBF_MAX`
- 手动 `cmds.loadPlugin('rbfmax_maya')`；如果报错消息包含"no such plugin"，说明 `.mod` 文件没被识别

### 卸载后又出现 mRBFNode 节点
- 卸载前先打开的场景里有保存的节点；下次打开该场景会提示 "Unknown Nodes"
- 忽略或用 `cmds.delete(cmds.ls(type='mRBFNode'))` 手动清理

---

## 文件说明

| 文件 | 作用 |
|------|------|
| `drag_drop_install.py` | 拖入 Maya 的入口，包含 `onMayaDroppedPythonFile` 和完整安装/卸载逻辑 |
| `package.py` | 从 CMake 构建产物复制 .mll 到 installer/plug-ins/<version>/ |
| `README.md` | 本文件 |
| `plug-ins/<version>/rbfmax_maya.mll` | 编译好的 Maya 插件，由 `package.py` 填充 |
| `scripts/` | 预留目录，Phase 2C Qt UI 脚本未来放这里 |
