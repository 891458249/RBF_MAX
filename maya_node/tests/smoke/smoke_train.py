"""
Slice 12 mayapy smoke test — rbfmaxTrainAndSave MPxCommand.

Exercises four scenarios end-to-end:
    S1. CSV mode success   — train from two .csv files, save to a
                             fresh JSON, load it via mRBFNode, and
                             assert predict output bit-identical to
                             Slice 11's tiny_rbf_expected.json.
    S2. Inline mode success — same but via Python list arguments.
    S3. --force gate        — training into an existing file without
                             -force must raise.
    S4. Kernel string error — "Nonsense" must raise.

Usage:
    <mayapy> smoke_train.py <plugin_path> <fixtures_dir> <expected_json>

where `fixtures_dir` contains tiny_train_centers.csv,
tiny_train_targets.csv, and tiny_rbf_expected.json.

Exit codes:
    0 — all 4 scenarios passed
    1 — any failure
"""

from __future__ import print_function

import json
import os
import sys
import tempfile


def _load_interpolator_and_assert(cmds, node_type, plugin_basename,
                                  json_path, expected):
    """Helper: load JSON via mRBFNode and compare to expected outputs."""
    node = cmds.createNode(node_type)
    try:
        cmds.setAttr("{0}.jsonPath".format(node), json_path,
                     type="string")
        for q_entry in expected["queries"]:
            q = q_entry["query"]
            exp = q_entry["expected"]
            cmds.setAttr("{0}.queryPoint".format(node), q,
                         type="doubleArray")
            got = cmds.getAttr("{0}.outputValues".format(node))
            assert got is not None, \
                "outputValues is None (statusMessage={0!r})".format(
                    cmds.getAttr("{0}.statusMessage".format(node)))
            assert len(got) == len(exp), \
                "length {0} != {1}".format(len(got), len(exp))
            for j in range(len(exp)):
                err = abs(got[j] - exp[j])
                assert err < 1e-10, \
                    "q={0} comp {1}: got {2}, exp {3}, err {4}".format(
                        q, j, got[j], exp[j], err)
    finally:
        cmds.delete(node)
        cmds.flushUndo()


def main():
    if len(sys.argv) != 4:
        print("usage: mayapy smoke_train.py <plugin_path> <fixtures_dir> "
              "<expected_json>", file=sys.stderr)
        return 1

    plugin_path    = sys.argv[1]
    fixtures_dir   = sys.argv[2]
    expected_json  = sys.argv[3]

    if not os.path.isfile(plugin_path):
        print("plugin not found: {0}".format(plugin_path), file=sys.stderr)
        return 1

    centers_csv = os.path.abspath(
        os.path.join(fixtures_dir, "tiny_train_centers.csv"))
    targets_csv = os.path.abspath(
        os.path.join(fixtures_dir, "tiny_train_targets.csv"))
    for p in (centers_csv, targets_csv, expected_json):
        if not os.path.isfile(p):
            print("fixture not found: {0}".format(p), file=sys.stderr)
            return 1

    with open(expected_json, "r") as f:
        expected = json.load(f)

    tempdir = tempfile.mkdtemp(prefix="rbfmax_smoke_train_")
    csv_out_path    = os.path.join(tempdir, "train_csv_out.json")
    inline_out_path = os.path.join(tempdir, "train_inline_out.json")

    import maya.standalone
    maya.standalone.initialize(name="python")

    try:
        import maya.cmds as cmds

        cmds.loadPlugin(plugin_path, quiet=False)
        print("[0/4] loadPlugin OK: {0}".format(plugin_path))
        plugin_basename = os.path.splitext(os.path.basename(plugin_path))[0]

        # --- S1: CSV mode ----------------------------------------------
        # Note: Maya's MSyntax::addFlag silently rejects long flag names
        # shorter than 4 characters, so -eps/-dim had to be spelled as
        # -epsilon and -inputDim.  Python keyword args use the same
        # spelling.
        ret = cmds.rbfmaxTrainAndSave(
            centersFile=centers_csv,
            targetsFile=targets_csv,
            jsonPath=csv_out_path,
            kernel="Gaussian", epsilon=1.0, polyDegree=-1,
            force=True,
            **{"lambda": "1e-6"})
        assert ret == csv_out_path, \
            "expected return value {0!r}, got {1!r}".format(
                csv_out_path, ret)
        assert os.path.isfile(csv_out_path), \
            "csv-mode output not written: {0}".format(csv_out_path)
        _load_interpolator_and_assert(
            cmds, "mRBFNode", plugin_basename, csv_out_path, expected)
        print("[1/4] S1 csv-mode train + load + predict bit-identical OK")

        # --- S2: inline mode -------------------------------------------
        ret2 = cmds.rbfmaxTrainAndSave(
            centers=[0.0, 0.0,  1.0, 0.0,  0.0, 1.0,  1.0, 1.0],
            targets=[0.0, 1.0, 1.0, 2.0],
            inputDim=2, outputDim=1,
            jsonPath=inline_out_path,
            kernel="Gaussian", epsilon=1.0, polyDegree=-1,
            force=True,
            **{"lambda": "1e-6"})
        assert ret2 == inline_out_path
        assert os.path.isfile(inline_out_path)
        _load_interpolator_and_assert(
            cmds, "mRBFNode", plugin_basename, inline_out_path, expected)
        print("[2/4] S2 inline-mode train + load + predict bit-identical OK")

        # --- S3: --force gate ------------------------------------------
        raised = False
        try:
            cmds.rbfmaxTrainAndSave(
                centersFile=centers_csv,
                targetsFile=targets_csv,
                jsonPath=csv_out_path,            # already exists from S1
                kernel="Gaussian", epsilon=1.0, polyDegree=-1,
                force=False,
                **{"lambda": "1e-6"})
        except RuntimeError as exc:
            raised = True
            msg = str(exc).lower()
            assert "file exists" in msg or "exists" in msg, \
                "S3 error text unexpected: {0!r}".format(exc)
        assert raised, "S3 should have raised RuntimeError on existing file"
        print("[3/4] S3 force=False on existing file -> RuntimeError OK")

        # --- S4: bogus kernel string -----------------------------------
        raised = False
        try:
            cmds.rbfmaxTrainAndSave(
                centersFile=centers_csv,
                targetsFile=targets_csv,
                jsonPath=os.path.join(tempdir, "bogus.json"),
                kernel="Nonsense", epsilon=1.0, polyDegree=-1,
                force=True,
                **{"lambda": "1e-6"})
        except RuntimeError as exc:
            raised = True
            msg = str(exc).lower()
            assert ("unknown kernel" in msg) or ("nonsense" in msg), \
                "S4 error text unexpected: {0!r}".format(exc)
        assert raised, "S4 should have raised RuntimeError on bad kernel"
        print("[4/4] S4 kernel='Nonsense' -> RuntimeError OK")

        cmds.unloadPlugin(plugin_basename)
        print("\n=== Slice 12 mayapy train smoke: PASS ===")
        return 0

    except Exception as exc:  # noqa: BLE001
        print("[FAIL] {0}".format(exc), file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1

    finally:
        maya.standalone.uninitialize()
        # Clean up tempdir (best-effort).
        try:
            for fname in os.listdir(tempdir):
                try:
                    os.remove(os.path.join(tempdir, fname))
                except OSError:
                    pass
            os.rmdir(tempdir)
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
