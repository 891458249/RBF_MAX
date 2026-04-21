"""
Slice 11 mayapy smoke test — real predict via JSON-path load.

Runs the 5-step Slice 11 contract end-to-end:
    1. loadPlugin
    2. createNode("mRBFNode")
    3. setAttr jsonPath + read state attributes (isLoaded, nCenters,
       dimInput, dimOutput, kernelType, statusMessage)
    4. For each fixture query point, setAttr queryPoint + getAttr
       outputValues + assert bit-identical to expected
    5. delete + flushUndo + unloadPlugin

Usage (local, Maya 2022 or 2025):
    <mayapy> smoke_predict.py <plugin_path> <fixture_dir>

where fixture_dir contains tiny_rbf.json and tiny_rbf_expected.json.

Exit codes:
    0 — all 5 steps passed
    1 — any failure

Tolerance: 1e-10 absolute.  The Maya plugin calls Phase 1's same
RBFInterpolator::load + predict as the generator, so in theory
err=0 exactly.  1e-10 margin is for any DG-internal double round-trip
(unexpected but cheap to guard against).
"""

from __future__ import print_function

import json
import math
import os
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mayapy smoke_predict.py <plugin_path> <fixture_dir>",
              file=sys.stderr)
        return 1

    plugin_path = sys.argv[1]
    fixture_dir = sys.argv[2]

    if not os.path.isfile(plugin_path):
        print("plugin not found: {0}".format(plugin_path), file=sys.stderr)
        return 1

    rbf_json = os.path.abspath(os.path.join(fixture_dir, "tiny_rbf.json"))
    expected_json = os.path.abspath(
        os.path.join(fixture_dir, "tiny_rbf_expected.json"))
    for p in (rbf_json, expected_json):
        if not os.path.isfile(p):
            print("fixture not found: {0}".format(p), file=sys.stderr)
            return 1

    with open(expected_json, "r") as f:
        expected = json.load(f)

    import maya.standalone
    maya.standalone.initialize(name="python")

    try:
        import maya.cmds as cmds

        # Step 1
        cmds.loadPlugin(plugin_path, quiet=False)
        print("[1/5] loadPlugin OK: {0}".format(plugin_path))

        # Step 2
        node = cmds.createNode("mRBFNode")
        print("[2/5] createNode OK: {0}".format(node))

        # Step 3 — set jsonPath and verify state attributes
        cmds.setAttr("{0}.jsonPath".format(node), rbf_json, type="string")
        is_loaded = cmds.getAttr("{0}.isLoaded".format(node))
        n_centers = cmds.getAttr("{0}.nCenters".format(node))
        dim_in    = cmds.getAttr("{0}.dimInput".format(node))
        dim_out   = cmds.getAttr("{0}.dimOutput".format(node))
        kernel_t  = cmds.getAttr("{0}.kernelType".format(node))
        status    = cmds.getAttr("{0}.statusMessage".format(node))
        print("[3/5] state: isLoaded={0}, nCenters={1}, dimInput={2}, "
              "dimOutput={3}, kernelType={4!r}, statusMessage={5!r}".format(
                  is_loaded, n_centers, dim_in, dim_out, kernel_t, status))
        assert is_loaded is True, "expected isLoaded=True"
        assert n_centers == 4, "expected nCenters=4, got {0}".format(n_centers)
        assert dim_in == 2,    "expected dimInput=2, got {0}".format(dim_in)
        assert dim_out == 1,   "expected dimOutput=1, got {0}".format(dim_out)
        assert kernel_t == "Gaussian", \
            "expected kernelType='Gaussian', got {0!r}".format(kernel_t)
        assert status == "OK", "expected statusMessage='OK', got {0!r}".format(status)

        # Step 4 — predict each fixture query and compare to expected.
        # setAttr for doubleArray takes the python list directly (NOT
        # count-prefixed unpacked args — that form silently truncates
        # to one element in practice, per Maya 2022/2025 behaviour).
        for i, q_entry in enumerate(expected["queries"]):
            q = q_entry["query"]
            exp = q_entry["expected"]
            cmds.setAttr("{0}.queryPoint".format(node),
                         q, type="doubleArray")
            # Defensive: verify the plug now holds the full array.
            qr = cmds.getAttr("{0}.queryPoint".format(node))
            assert qr and len(qr) == len(q), (
                "queryPoint readback length {0} != expected {1} (got {2!r})"
                .format(len(qr) if qr else None, len(q), qr))

            got = cmds.getAttr("{0}.outputValues".format(node))
            # getAttr on MFnData::kDoubleArray returns a plain list in
            # Maya 2022/2025 Python bindings.
            assert got is not None, (
                "query {0}: outputValues is None (statusMessage={1!r})"
                .format(i, cmds.getAttr("{0}.statusMessage".format(node))))
            assert len(got) == len(exp), (
                "query {0}: length {1} != expected length {2} (got {3!r})"
                .format(i, len(got), len(exp), got))
            for j in range(len(exp)):
                err = abs(got[j] - exp[j])
                assert err < 1e-10, (
                    "query {0} comp {1}: got={2}, exp={3}, err={4}".format(
                        i, j, got[j], exp[j], err))
            print("[4/5] query {0} q={1} -> {2} (exp {3}) err<=1e-10 OK"
                  .format(i, q, got, exp))

        # Step 5 — cleanup
        cmds.delete(node)
        cmds.flushUndo()
        plugin_basename = os.path.splitext(os.path.basename(plugin_path))[0]
        cmds.unloadPlugin(plugin_basename)
        print("[5/5] delete + flushUndo + unloadPlugin OK")

        print("\n=== Slice 11 mayapy predict smoke: PASS ===")
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
