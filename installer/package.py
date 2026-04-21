# -*- coding: utf-8 -*-
"""
RBF_MAX — 打包脚本

把 CMake 构建产物 (build-maya-2022 / build-maya-2025 下的
rbfmax_maya.mll) 复制到 installer/plug-ins/<version>/ 以形成可分发的
installer/ 文件夹（用户拖 drag_drop_install.py 到 Maya 就能装）。

用法：
    python installer/package.py

要求：
    * build-maya-2022/bin/Release/rbfmax_maya.mll 已构建
    * build-maya-2025/bin/Release/rbfmax_maya.mll 已构建
"""
from __future__ import print_function

import os
import shutil
import sys

REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), os.pardir))
INSTALLER_DIR = os.path.join(REPO_ROOT, "installer")

VERSIONS = ("2022", "2025")


def _source(ver):
    return os.path.join(REPO_ROOT,
                        "build-maya-" + ver,
                        "bin", "Release",
                        "rbfmax_maya.mll")


def _target(ver):
    d = os.path.join(INSTALLER_DIR, "plug-ins", ver)
    if not os.path.isdir(d):
        os.makedirs(d)
    return os.path.join(d, "rbfmax_maya.mll")


def main():
    # -- Build-missing preflight (fail-fast, strict) --------------------
    # The installer only makes sense as a fully-packaged artifact; a
    # partial zip missing one version's .mll would silently ship to
    # users and fail at install time with a cryptic error.  Enforce
    # "all-or-nothing".
    missing = []
    for ver in VERSIONS:
        src = _source(ver)
        if not os.path.isfile(src):
            missing.append((ver, src))

    if missing:
        print("ERROR: Maya plugin build missing for version(s): "
              + ", ".join(v for v, _ in missing), file=sys.stderr)
        print("", file=sys.stderr)
        print("Expected build artifact(s):", file=sys.stderr)
        for ver, src in missing:
            print("  Maya {v}:  {s}".format(v=ver, s=src),
                  file=sys.stderr)
        print("", file=sys.stderr)
        print("Please build the plugin first for each missing version:",
              file=sys.stderr)
        print("", file=sys.stderr)
        print("  cmake -S . -B build-maya-<VER>"
              " -G \"Visual Studio 17 2022\" -A x64 \\",
              file=sys.stderr)
        print("        -DCMAKE_BUILD_TYPE=Release \\",
              file=sys.stderr)
        print("        -DRBF_BUILD_MAYA_NODE=ON"
              " -DMAYA_VERSION=<VER> \\",
              file=sys.stderr)
        print("        -DMAYA_DEVKIT_ROOT=\"<path/to/devkit>\"",
              file=sys.stderr)
        print("  cmake --build build-maya-<VER> --config Release",
              file=sys.stderr)
        print("", file=sys.stderr)
        print("See maya_node/README.md for full build instructions.",
              file=sys.stderr)
        return 1

    # -- Copy payloads --------------------------------------------------
    ok = []
    for ver in VERSIONS:
        src = _source(ver)
        dst = _target(ver)
        shutil.copy2(src, dst)
        size_kb = os.path.getsize(dst) / 1024.0
        ok.append((ver, dst, size_kb))
        print("[OK] Maya {v}: {d}  ({s:.1f} KiB)".format(
            v=ver, d=dst, s=size_kb))

    print()
    print("Packaged {0} plugin version(s) to installer/plug-ins/".format(
        len(ok)))
    print("End users: drag installer/drag_drop_install.py into "
          "Maya viewport to install.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
