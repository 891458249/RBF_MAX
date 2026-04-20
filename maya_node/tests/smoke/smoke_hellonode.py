"""
Slice 10A mayapy smoke test.

Exercises the 4-step D10 contract end-to-end:
    1. loadPlugin          — proves MFnPlugin registration works
    2. createNode          — proves mRBFNode::creator + initialize
    3. setAttr + getAttr + assert — proves compute() routes through
                                    the Phase 1 Gaussian kernel
    4. delete + unloadPlugin — proves uninitializePlugin is clean

Usage (local, Maya 2022):
    "C:/Program Files/Autodesk/Maya2022/bin/mayapy.exe" \\
        maya_node/tests/smoke/smoke_hellonode.py \\
        build-maya-2022/bin/rbfmax_maya.mll

Exit codes:
    0 — all 4 assertions passed
    1 — any failure (plugin path missing, load/create/compute failure,
        assertion violation, unload failure)

Tolerance: 1e-12 absolute.  setAttr/getAttr marshal doubles through
Maya's DG which in theory preserves bit-identical doubles, but 1e-12
is the conservative upper bound against any internal formatting drift
that might sneak in on specific Maya versions.
"""

from __future__ import print_function

import math
import os
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: mayapy smoke_hellonode.py <plugin_path>",
              file=sys.stderr)
        return 1

    plugin_path = sys.argv[1]
    if not os.path.isfile(plugin_path):
        print("plugin not found: {}".format(plugin_path), file=sys.stderr)
        return 1

    # maya.standalone initialisation must happen BEFORE any maya.cmds call.
    import maya.standalone
    maya.standalone.initialize(name="python")

    try:
        import maya.cmds as cmds

        # Step 1 — loadPlugin.  quiet=False so any registration error is
        # surfaced to stderr in this non-interactive run.
        cmds.loadPlugin(plugin_path, quiet=False)
        print("[1/4] loadPlugin OK: {}".format(plugin_path))

        # Step 2 — createNode by type name (matches mRBFNode::kTypeName).
        node = cmds.createNode("mRBFNode")
        print("[2/4] createNode OK: {}".format(node))

        # Step 3 — compute contract.  Input 1.0 must drive output to
        # exp(-1) via the Gaussian kernel.
        cmds.setAttr("{0}.inputValue".format(node), 1.0)
        out = cmds.getAttr("{0}.outputValue".format(node))
        expected = math.exp(-1.0)
        err = abs(out - expected)
        assert err < 1e-12, (
            "compute(1.0) = {0}, expected {1} (|err|={2} > 1e-12)"
            .format(out, expected, err))
        print("[3/4] compute(1.0) = {0} vs expected {1} (err={2:.3e}) OK"
              .format(out, expected, err))

        # Step 4 — cleanup.  delete the node, then flushUndo() so the
        # undo stack releases its reference (otherwise unloadPlugin
        # refuses with "plugin still in use"), then unload by the
        # plugin's filename (Maya stores plugins by basename without
        # extension).
        cmds.delete(node)
        cmds.flushUndo()
        plugin_basename = os.path.splitext(os.path.basename(plugin_path))[0]
        cmds.unloadPlugin(plugin_basename)
        print("[4/4] delete + flushUndo + unloadPlugin OK")

        print("\n=== Slice 10A mayapy smoke: PASS ===")
        return 0

    except Exception as exc:  # noqa: BLE001 — mayapy exceptions are varied
        print("[FAIL] {0}".format(exc), file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1

    finally:
        maya.standalone.uninitialize()


if __name__ == "__main__":
    sys.exit(main())
