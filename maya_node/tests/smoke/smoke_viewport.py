"""
Slice 13 mayapy smoke — Path B architecture verification.

mayapy -batch has no GL context, so this script cannot verify actual
drawing.  What it CAN verify (and does):

    1. mRBFNode is still a kDependNode and has no drawdb classification
       (Phase 2A contract unchanged under Path B).
    2. mRBFShape type is registered (Slice 13 new).
    3. mRBFShape carries the Viewport 2.0 classification our
       DrawOverride registers against — this is the most common way
       to break Viewport 2.0 draw silently.
    4. mRBFNode.message → mRBFShape.sourceNode connection works.
    5. Phase 2A predict round-trip on mRBFNode still works.
    6. mRBFShape's own attrs (drawEnabled, sphereRadius) are functional.

Usage:
    <mayapy> smoke_viewport.py <plugin_path> <tiny_rbf_json>

Exit: 0 = all assertions passed; 1 = any failure.
"""

from __future__ import print_function

import os
import sys


def main():
    if len(sys.argv) != 3:
        print("usage: mayapy smoke_viewport.py <plugin_path> <tiny_rbf_json>",
              file=sys.stderr)
        return 1

    plugin_path = sys.argv[1]
    json_path   = sys.argv[2]

    if not os.path.isfile(plugin_path):
        print("plugin not found: {0}".format(plugin_path), file=sys.stderr)
        return 1
    if not os.path.isfile(json_path):
        print("json not found: {0}".format(json_path), file=sys.stderr)
        return 1

    import maya.standalone
    maya.standalone.initialize(name="python")

    try:
        import maya.cmds as cmds

        cmds.loadPlugin(plugin_path, quiet=False)
        print("[0/7] loadPlugin OK: {0}".format(plugin_path))

        # 1. mRBFNode still registered; Phase 2A unchanged.
        #    Note: cmds.objectType(name, isAType=True) requires `name` to
        #    be an actual scene node; use allNodeTypes() for type-table
        #    membership instead (Maya idiom).
        all_types = cmds.allNodeTypes() or []
        assert "mRBFNode" in all_types, \
            "mRBFNode type is not registered"
        n_classif = cmds.getClassification("mRBFNode")
        n_joined = (" ".join(n_classif) if isinstance(n_classif, list)
                    else (n_classif or ""))
        assert "drawdb" not in n_joined, (
            "mRBFNode should NOT carry drawdb classification under Path B; "
            "got {0!r}".format(n_joined))
        print("[1/7] mRBFNode type registered + no drawdb classification "
              "(Phase 2A unchanged)")

        # 2. mRBFShape type registered.
        assert "mRBFShape" in all_types, \
            "mRBFShape type is not registered"
        print("[2/7] mRBFShape type is registered")

        # 3. mRBFShape classification matches DrawOverride literal.
        s_classif = cmds.getClassification("mRBFShape")
        s_joined = (" ".join(s_classif) if isinstance(s_classif, list)
                    else (s_classif or ""))
        expected = "drawdb/geometry/rbfmax/mRBFShape"
        assert expected in s_joined, (
            "mRBFShape classification {0!r} does not contain {1!r} — "
            "DrawOverride will be silently skipped".format(
                s_joined, expected))
        print("[3/7] mRBFShape classification aligned: {0!r}".format(s_joined))

        # 4. Create both + connect.
        rbf_node = cmds.createNode("mRBFNode")
        shape    = cmds.createNode("mRBFShape")
        cmds.connectAttr("{0}.message".format(rbf_node),
                         "{0}.sourceNode".format(shape))
        print("[4/7] created + connected: {0}.message -> {1}.sourceNode"
              .format(rbf_node, shape))

        # 5. Phase 2A predict round-trip still works.
        cmds.setAttr("{0}.jsonPath".format(rbf_node), json_path, type="string")
        cmds.setAttr("{0}.queryPoint".format(rbf_node), [0.5, 0.5],
                     type="doubleArray")
        out = cmds.getAttr("{0}.outputValues".format(rbf_node))
        assert out is not None, "outputValues is None after predict"
        assert len(out) == 1, \
            "expected 1 output row, got {0}".format(len(out))
        print("[5/7] mRBFNode predict [0.5, 0.5] -> {0} (Phase 2A clean)"
              .format(out))

        # 6. State attrs mRBFDrawOverride's prepareForDraw reads are
        #    exposed on mRBFNode (still) AND mRBFShape has its own.
        assert cmds.getAttr("{0}.isLoaded".format(rbf_node)) is True, \
            "isLoaded must be True after jsonPath load"
        n_centers = cmds.getAttr("{0}.nCenters".format(rbf_node))
        assert n_centers == 4, \
            "expected nCenters=4 for tiny_rbf fixture, got {0}".format(
                n_centers)
        print("[6/7] state attrs reflect loaded interp (nCenters={0})"
              .format(n_centers))

        # 7. mRBFShape attrs functional.
        assert cmds.getAttr("{0}.drawEnabled".format(shape)) is True
        cmds.setAttr("{0}.sphereRadius".format(shape), 0.1)
        r = cmds.getAttr("{0}.sphereRadius".format(shape))
        assert abs(r - 0.1) < 1e-12, \
            "sphereRadius round-trip: expected 0.1 got {0}".format(r)
        print("[7/7] mRBFShape attrs functional (drawEnabled=True, "
              "sphereRadius round-trips)")

        # Cleanup.
        cmds.delete(shape, rbf_node)
        cmds.flushUndo()
        plugin_basename = os.path.splitext(os.path.basename(plugin_path))[0]
        cmds.unloadPlugin(plugin_basename)
        print("[done] delete + flushUndo + unloadPlugin OK")

        print("\n=== Slice 13 mayapy viewport smoke (Path B): PASS ===")
        return 0

    except Exception as exc:  # noqa: BLE001
        print("[FAIL] {0}".format(exc), file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1

    finally:
        maya.standalone.uninitialize()


if __name__ == "__main__":
    sys.exit(main())
