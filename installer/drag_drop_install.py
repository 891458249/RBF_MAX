# -*- coding: utf-8 -*-
# =============================================================================
# RBF_MAX — Maya 拖放安装脚本
# -----------------------------------------------------------------------------
# 使用方法：
#   1. 把这个 .py 文件直接拖入 Maya 视窗（任意版本 Maya 2022 / 2025）
#   2. 在弹出的对话框里选择 "安装" 或 "卸载"
#
# 技术路径：
#   * 基于 Maya Module System（.mod 文件 + modules 目录），不写入 Maya 的
#     内置 plug-ins 目录，卸载时完全可逆。
#   * 安装目标目录：
#       Windows : %USERPROFILE%/Documents/maya/modules/
#       macOS   : ~/Library/Preferences/Autodesk/maya/modules/
#       Linux   : ~/maya/modules/
#   * Maya 下次启动时会自动识别模块，或当前会话中调用
#     MGlobal::executeCommand('loadModule -load "<mod_path>"') 立即生效。
#
# 支持的 Maya 版本: 2022, 2025
# =============================================================================
from __future__ import print_function

import os
import shutil
import sys
import traceback
import gc
import time

try:
    import maya.cmds as cmds
    import maya.mel as mel
    _IN_MAYA = True
except ImportError:
    _IN_MAYA = False
    cmds = None
    mel = None


# -----------------------------------------------------------------------------
# 常量
# -----------------------------------------------------------------------------
PLUGIN_NAME      = "rbfmax_maya"          # .mll 文件名（不含扩展）
MODULE_NAME      = "RBF_MAX"              # Maya module 名
MODULE_VERSION   = "1.1.0"
SUPPORTED_VERSIONS = ("2022", "2025")

# 随安装包分发的相对路径：plug-ins/<version>/rbfmax_maya.mll
_REL_PLUGIN_DIR  = "plug-ins"


# -----------------------------------------------------------------------------
# 工具函数
# -----------------------------------------------------------------------------
def _installer_dir():
    """返回本脚本所在目录的绝对路径（拖放或 execfile 均可用）。"""
    try:
        return os.path.abspath(os.path.dirname(__file__))
    except NameError:
        # 极少数 Maya 版本下 __file__ 未定义；尝试从栈帧取
        import inspect
        frame = inspect.currentframe()
        return os.path.abspath(os.path.dirname(
            inspect.getsourcefile(frame) or os.getcwd()))


def _maya_version():
    """返回字符串 '2022' / '2025' 等。"""
    if not _IN_MAYA:
        raise RuntimeError("This script must be run inside Maya.")
    return str(cmds.about(version=True)).strip().split()[0]


def _user_modules_dir():
    """返回用户 Maya modules 目录（创建如不存在）。

    FIX (slice-13 installer followup): 原实现用 os.path.expanduser("~")
    解析 Windows 家目录。当用户设了 HOME 环境变量（Git Bash / MSYS /
    Cygwin / WSL 等会改它指向 Documents），expanduser("~") 返回
    C:/Users/<user>/Documents 而非 C:/Users/<user>，拼接出双
    "Documents/Documents/maya/modules"，.mod 文件落到 Maya 扫描不到的
    位置，用户看到安装成功但插件无法加载。

    修复方案: Maya 内优先用 cmds.internalVar(userAppDir=True)（Maya 内部
    用 SHGetFolderPath 解析，不受 HOME 干扰）。非 Maya fallback 在
    Windows 下强制用 USERPROFILE（Windows 标准、不被 Unix 工具污染）。
    """
    base = None
    if _IN_MAYA:
        try:
            # Maya 的路径解析不受 HOME 干扰
            user_app_dir = cmds.internalVar(userAppDir=True)
            if user_app_dir:
                # userAppDir 通常带尾斜杠: "C:/Users/.../Documents/maya/"
                base = user_app_dir.rstrip("/").rstrip("\\")
        except Exception:  # noqa: BLE001
            base = None

    if base is None:
        if sys.platform == "win32":
            # Fallback: 优先 USERPROFILE，不用 expanduser("~")
            user_home = (os.environ.get("USERPROFILE")
                         or os.path.expanduser("~"))
            base = os.path.join(user_home, "Documents", "maya")
        elif sys.platform == "darwin":
            base = os.path.expanduser(
                "~/Library/Preferences/Autodesk/maya")
        else:
            base = os.path.expanduser("~/maya")

    modules_dir = os.path.join(base, "modules").replace("\\", "/")
    if not os.path.isdir(modules_dir):
        os.makedirs(modules_dir)
    return modules_dir


def _module_target_root():
    """模块被安装到的目标根目录 (<modules_dir>/RBF_MAX)."""
    return os.path.join(_user_modules_dir(), MODULE_NAME)


def _module_file_path():
    """模块描述文件 (<modules_dir>/RBF_MAX.mod) 的绝对路径。"""
    return os.path.join(_user_modules_dir(), MODULE_NAME + ".mod")


def _source_mll(version):
    """安装包内的 .mll 源路径。"""
    return os.path.join(_installer_dir(),
                        _REL_PLUGIN_DIR,
                        version,
                        PLUGIN_NAME + ".mll")


def _message(msg, icon="information"):
    """在 Maya 里弹对话框，或 fallback 到 print。"""
    if _IN_MAYA:
        cmds.confirmDialog(
            title="RBF_MAX " + MODULE_VERSION,
            message=msg,
            button=["确定"],
            icon=icon)
    else:
        print(msg)


def _log(msg):
    """写入 Maya Script Editor + stdout。"""
    line = "[RBF_MAX installer] " + msg
    print(line)
    if _IN_MAYA:
        try:
            cmds.inViewMessage(amg=line, pos="midCenter", fade=True)
        except Exception:  # noqa: BLE001
            pass


# -----------------------------------------------------------------------------
# 安装
# -----------------------------------------------------------------------------
def _write_module_file(module_root, version):
    """
    写 RBF_MAX.mod 文件。

    Maya 模块文件语法：
        + MAYAVERSION:<v> <NAME> <VERSION> <ABSOLUTE_PATH>
        plug-ins: plug-ins/<v>
        scripts:  scripts
    """
    mod_path = _module_file_path()
    lines = []
    for v in SUPPORTED_VERSIONS:
        # 每个支持的版本一行；Maya 会只挑对应自己版本的那行生效。
        lines.append("+ MAYAVERSION:{ver} {name} {vn} {root}".format(
            ver=v,
            name=MODULE_NAME,
            vn=MODULE_VERSION,
            root=module_root.replace("\\", "/")))
        lines.append("plug-ins: {rel}/{ver}".format(
            rel=_REL_PLUGIN_DIR, ver=v))
        lines.append("scripts: scripts")
        lines.append("")  # 空行分隔版本块

    with open(mod_path, "w") as f:
        f.write("\n".join(lines))
    _log("wrote module file: " + mod_path)


def _safe_rmtree(path, retries=5, delay=0.2):
    """Windows 下 .mll 句柄可能延迟释放；rmtree 加 retry。
    成功返回 None；全部失败返回最后捕获的异常（不抛）。"""
    if not os.path.isdir(path):
        return None
    last_err = None
    for _ in range(retries):
        try:
            shutil.rmtree(path)
            return None
        except (PermissionError, OSError) as e:
            last_err = e
            time.sleep(delay)
    return last_err


def _force_overwrite_tree(src, dst):
    """文件级覆盖；失败的单文件记录但不中断。返回失败列表。"""
    failures = []
    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        dst_root = os.path.join(dst, rel) if rel != '.' else dst
        if not os.path.isdir(dst_root):
            try:
                os.makedirs(dst_root)
            except Exception:  # noqa: BLE001
                pass
        for f in files:
            src_f = os.path.join(root, f)
            dst_f = os.path.join(dst_root, f)
            try:
                if os.path.isfile(dst_f):
                    os.remove(dst_f)
                shutil.copy2(src_f, dst_f)
            except Exception as e:  # noqa: BLE001
                failures.append((dst_f, str(e)))
    return failures


def _copy_payload(module_root):
    """把 installer 目录里的 plug-ins/ 和 scripts/ 复制到 module_root。

    Windows 注意: 若 dst 中的 .mll 当前被 Maya 持有锁，rmtree 会失败。
    此时退化为文件级覆盖（_force_overwrite_tree）：单个 .mll 写不进去
    时记录而不中断，让 _install 继续完成 .mod 写入；下次启动 Maya
    autoload 仍然生效。"""
    src_root = _installer_dir()
    for sub in (_REL_PLUGIN_DIR, "scripts"):
        src = os.path.join(src_root, sub)
        dst = os.path.join(module_root, sub)
        if os.path.isdir(dst):
            err = _safe_rmtree(dst)
            if err is not None:
                _log("rmtree failed ({0}); falling back to file-level "
                     "overwrite".format(err))
                if os.path.isdir(src):
                    failures = _force_overwrite_tree(src, dst)
                    if failures:
                        _log("file-level overwrite had {0} failure(s); "
                             "first: {1}".format(len(failures), failures[0]))
                    else:
                        _log("file-level overwrite ok: {0} -> {1}".format(
                            src, dst))
                continue
        if os.path.isdir(src):
            shutil.copytree(src, dst)
            _log("copied {s} -> {d}".format(s=src, d=dst))
        else:
            # scripts 可能不存在，跳过
            if not os.path.isdir(dst):
                os.makedirs(dst)


def _install():
    version = _maya_version()
    if version not in SUPPORTED_VERSIONS:
        _message(
            "当前 Maya 版本 {v} 未在支持列表内。\n"
            "支持的版本：{sup}".format(
                v=version, sup=", ".join(SUPPORTED_VERSIONS)),
            icon="warning")
        return

    src_mll = _source_mll(version)
    if not os.path.isfile(src_mll):
        _message(
            "在安装包中找不到 Maya {v} 对应的插件:\n  {p}\n\n"
            "请先构建插件（参见 README）。".format(
                v=version, p=src_mll),
            icon="critical")
        return

    module_root = _module_target_root()
    mod_file    = _module_file_path()

    # 如果已经安装，先卸载再重装（清洁升级路径）。
    if os.path.isfile(mod_file) or os.path.isdir(module_root):
        _log("existing installation detected; doing clean reinstall")
        _uninstall_core(quiet=True)

    # 1) 建目录 + 拷贝 payload
    if not os.path.isdir(module_root):
        os.makedirs(module_root)
    _copy_payload(module_root)

    # 2) 写 .mod 文件
    _write_module_file(module_root, module_root)

    # 3) 当前会话中立即加载模块 + 插件
    try:
        mel.eval('loadModule -load "{0}"'.format(
            mod_file.replace("\\", "/")))
    except Exception as e:  # noqa: BLE001
        # 某些 Maya 版本 loadModule 不存在或行为差异；不致命。
        _log("loadModule mel eval warning: " + str(e))

    # 刷新插件搜索路径
    try:
        plug_in_dir = os.path.join(
            module_root, _REL_PLUGIN_DIR, version)
        if os.environ.get("MAYA_PLUG_IN_PATH", ""):
            os.environ["MAYA_PLUG_IN_PATH"] = (
                plug_in_dir + os.pathsep +
                os.environ["MAYA_PLUG_IN_PATH"])
        else:
            os.environ["MAYA_PLUG_IN_PATH"] = plug_in_dir
    except Exception as e:  # noqa: BLE001
        _log("env update warning: " + str(e))

    # 4) 加载插件 + 设置自动加载
    try:
        cmds.loadPlugin(PLUGIN_NAME, quiet=False)
        cmds.pluginInfo(PLUGIN_NAME, edit=True, autoload=True)
    except Exception as e:  # noqa: BLE001
        _message(
            "模块已写入，但加载插件时发生错误:\n{0}\n\n"
            "请重启 Maya 后再试。".format(e),
            icon="warning")
        return

    _message(
        "✅ RBF_MAX {ver} 已成功安装到 Maya {mv}\n\n"
        "模块位置:\n  {root}\n\n"
        "插件已加载，下次启动 Maya 会自动加载。\n\n"
        "验证:\n  import maya.cmds as cmds\n"
        "  cmds.createNode('mRBFNode')\n"
        "  cmds.createNode('mRBFShape')".format(
            ver=MODULE_VERSION, mv=version, root=module_root))


# -----------------------------------------------------------------------------
# 卸载
# -----------------------------------------------------------------------------
def _uninstall_core(quiet=False):
    """核心卸载流程（供卸载入口 + 升级重装共用）。"""
    errors = []

    # 1) 卸载插件 + 清除 autoload
    if _IN_MAYA:
        try:
            if cmds.pluginInfo(PLUGIN_NAME, q=True, loaded=True):
                # 先删掉所有 mRBFNode / mRBFShape 场景节点，否则 unload 拒绝
                for ntype in ("mRBFShape", "mRBFNode"):
                    try:
                        nodes = cmds.ls(type=ntype) or []
                        if nodes:
                            cmds.delete(nodes)
                            _log("deleted {0} {1} node(s)".format(
                                len(nodes), ntype))
                    except Exception as e:  # noqa: BLE001
                        errors.append("delete {0}: {1}".format(ntype, e))
                cmds.flushUndo()
                cmds.unloadPlugin(PLUGIN_NAME)
                _log("unloaded plugin " + PLUGIN_NAME)
                # Windows: give FreeLibrary a moment to actually release
                # the DLL so the subsequent rmtree is not blocked.
                gc.collect()
                time.sleep(0.5)
        except Exception as e:  # noqa: BLE001
            errors.append("unloadPlugin: " + str(e))
        try:
            cmds.pluginInfo(PLUGIN_NAME, edit=True, autoload=False)
        except Exception:
            pass  # 未注册过就没 autoload 可清

    # 2) 删 .mod 文件
    mod_file = _module_file_path()
    if os.path.isfile(mod_file):
        try:
            os.remove(mod_file)
            _log("removed mod file: " + mod_file)
        except Exception as e:  # noqa: BLE001
            errors.append("remove mod: " + str(e))

    # 3) 删模块根目录（Windows 下 .mll 可能仍被 Maya 持有锁）
    module_root = _module_target_root()
    if os.path.isdir(module_root):
        err = _safe_rmtree(module_root)
        if err is None:
            _log("removed module dir: " + module_root)
        else:
            # 部分残留 — 列出残留文件方便用户判断
            remaining = []
            for r, d, fs in os.walk(module_root):
                for f in fs:
                    remaining.append(os.path.join(r, f))
            errors.append(
                "module dir partially removed ({0} files remain, "
                "likely .mll locked by Maya; restart Maya to clean): "
                "{1}".format(len(remaining), module_root))

    if errors and not quiet:
        _message(
            "卸载完成但有以下警告:\n\n" + "\n".join(errors),
            icon="warning")
    return errors


def _uninstall():
    if not (os.path.isfile(_module_file_path())
            or os.path.isdir(_module_target_root())):
        _message("未检测到已安装的 RBF_MAX。", icon="information")
        return
    # quiet=True so we own the dialog flow and can distinguish clean
    # vs partial outcomes by inspecting the returned errors list.
    errors = _uninstall_core(quiet=True)
    if not errors:
        _message(
            "✅ RBF_MAX 已完全卸载。\n\n"
            "被删除:\n  模块描述  {mod}\n  模块目录  {dir}".format(
                mod=_module_file_path(), dir=_module_target_root()))
    else:
        msg = (
            "⚠️ 卸载部分完成\n\n"
            "以下问题需要重启 Maya 后手动清理:\n\n"
            + "\n".join("  • " + e for e in errors)
            + "\n\nautoload 已关闭，下次启动 Maya 不会再加载该插件。\n"
            "重启 Maya 后可再次运行本 installer 的卸载来清残留。")
        _message(msg, icon="warning")


# -----------------------------------------------------------------------------
# 入口：拖放到 Maya 视窗时调用
# -----------------------------------------------------------------------------
def _run():
    if not _IN_MAYA:
        print("This script must be dragged into a Maya viewport.")
        return

    version = _maya_version()
    if version not in SUPPORTED_VERSIONS:
        _message(
            "检测到 Maya {v}；本安装包仅支持: {sup}".format(
                v=version, sup=", ".join(SUPPORTED_VERSIONS)),
            icon="warning")
        return

    already = (os.path.isfile(_module_file_path())
               or os.path.isdir(_module_target_root()))

    if already:
        # 已安装 — 提供"重新安装 / 卸载 / 取消"
        choice = cmds.confirmDialog(
            title="RBF_MAX " + MODULE_VERSION,
            message=(
                "RBF_MAX 已安装。\n\n"
                "模块位置:\n  {root}\n\n"
                "你想要?".format(root=_module_target_root())),
            button=["重新安装", "卸载", "取消"],
            defaultButton="取消",
            cancelButton="取消",
            dismissString="取消")
        if choice == "重新安装":
            _install()
        elif choice == "卸载":
            _uninstall()
        # 取消 — 不动
    else:
        # 未安装 — 提供"安装 / 取消"
        choice = cmds.confirmDialog(
            title="RBF_MAX " + MODULE_VERSION,
            message=(
                "欢迎使用 RBF_MAX 安装程序\n\n"
                "当前 Maya 版本: {v}\n"
                "安装目标目录:\n  {dir}\n\n"
                "是否安装?".format(
                    v=version, dir=_module_target_root())),
            button=["安装", "取消"],
            defaultButton="安装",
            cancelButton="取消",
            dismissString="取消")
        if choice == "安装":
            _install()


def onMayaDroppedPythonFile(obj):  # noqa: N802 (Maya API name)
    """Maya 把本 .py 文件拖入视窗时调用的入口。"""
    try:
        _run()
    except Exception as exc:  # noqa: BLE001
        tb = traceback.format_exc()
        print("[RBF_MAX installer] FATAL: {0}\n{1}".format(exc, tb))
        try:
            cmds.confirmDialog(
                title="RBF_MAX 安装程序错误",
                message="发生未预期错误:\n\n{0}\n\n{1}".format(exc, tb),
                button=["确定"],
                icon="critical")
        except Exception:
            pass


# 允许直接 `mayapy drag_drop_install.py install|uninstall` 用作命令行测试
if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("install", "uninstall"):
        # 手动初始化 maya.standalone
        try:
            import maya.standalone as mstand
            mstand.initialize(name="python")
            import maya.cmds as cmds  # noqa: F811
            import maya.mel as mel    # noqa: F811
            globals()["cmds"] = cmds
            globals()["mel"] = mel
            globals()["_IN_MAYA"] = True
            if sys.argv[1] == "install":
                _install()
            else:
                _uninstall()
        finally:
            try:
                import maya.standalone as mstand
                mstand.uninitialize()
            except Exception:
                pass
    else:
        print("此脚本设计为拖入 Maya 视窗运行。")
        print("也可用 mayapy 调用: "
              "mayapy drag_drop_install.py [install|uninstall]")
