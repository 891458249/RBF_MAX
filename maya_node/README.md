# rbfmax Maya plugin — Slice 10A skeleton

Phase 2A foundation: CMake + devkit integration + `mRBFNode` skeleton +
adapter-core that links the Phase 1 kernel into a Maya plugin.

**Slice 10A scope is validation-only.** The node has two scalar doubles
(`inputValue` / `outputValue`) and routes input through the Phase 1
Gaussian kernel to prove the toolchain works end-to-end. Real
`fit` / `predict` forwarding with dynamic array attributes lands in
Slice 11.

## Supported Maya versions

| Slice | Maya | C++ std | Status |
|------:|-----:|:-------:|:------:|
| 10A   | 2022 |  C++14  | ✅ validated (shift-left ABI anchor) |
| 10B   | 2024 |  C++17  | queued (requires Maya 2024 install) |
| 10C   | 2025 |  C++17  | ✅ validated (zero version-specific delta vs 10A) |
| 10D   | 2026 |  C++17  | queued (requires Maya 2026 install) |

Both Maya 2022 and Maya 2025 are locally validated end-to-end
(CMake configure, plugin link, `mayapy` smoke). 10B / 10D extend the
matrix once those devkits are on the development machine.

## Build — adapter tests (no Maya devkit required)

Pure-C++ adapter tests run on any CI node and cover the kernel-linking
layer. They are the CI-compatible half of Slice 10A.

```bash
cmake -S . -B build-adapter \
      -DCMAKE_BUILD_TYPE=Release \
      -DRBF_BUILD_MAYA_ADAPTER_TESTS=ON
cmake --build build-adapter -j
ctest --test-dir build-adapter --output-on-failure
```

This adds 3 TEST blocks (`HelloTransform.H1/H2/H3`) to the Phase 1 136,
yielding **139 tests green**.

## Build — Maya plugin (requires devkit)

### Build — Maya 2022

```bash
cmake -S . -B build-maya-2022 \
      -DCMAKE_BUILD_TYPE=Release \
      -DRBF_BUILD_MAYA_NODE=ON \
      -DMAYA_VERSION=2022 \
      -DMAYA_DEVKIT_ROOT="C:/Program Files/Autodesk/Maya2022"
cmake --build build-maya-2022 -j --config Release
```

The output is `build-maya-2022/bin/Release/rbfmax_maya.mll` (Windows),
`rbfmax_maya.so` (Linux), or `rbfmax_maya.bundle` (macOS).

### Build — Maya 2025

**Prerequisites**
- Maya 2025 devkit at `C:/SDK/Maya2025/devkitBase` (or any path you
  pass via `-DMAYA_DEVKIT_ROOT=...`). The Maya 2025 devkit ships as
  a separate archive from Autodesk; unpack it and move `devkitBase/`
  into a stable location outside the browser's Downloads folder.
- Maya 2025 runtime with `mayapy.exe` (for the smoke test step below).
  On Windows this is typically `C:/Program Files/Autodesk/Maya2025/bin/mayapy.exe`
  (Python 3.11).

```bash
cmake -S . -B build-maya-2025 \
      -DCMAKE_BUILD_TYPE=Release \
      -DRBF_BUILD_MAYA_NODE=ON \
      -DMAYA_VERSION=2025 \
      -DMAYA_DEVKIT_ROOT="C:/SDK/Maya2025/devkitBase"
cmake --build build-maya-2025 -j --config Release
```

Setting `MAYA_VERSION=2025` activates the C++17 arm of
`cmake/MayaVersionMatrix.cmake` (Maya 2022 builds at C++14; 2024,
2025, 2026 at C++17). `adapter_core.hpp` stays C++14-compatible so
both arms compile cleanly from the same source tree.

Output: `build-maya-2025/bin/Release/rbfmax_maya.mll`.

Smoke test:

```bash
"C:/Program Files/Autodesk/Maya2025/bin/mayapy.exe" \
    maya_node/tests/smoke/smoke_hellonode.py \
    build-maya-2025/bin/Release/rbfmax_maya.mll
```

Expected: `exit 0`, `compute(1.0)` bit-identical to `exp(-1)`
(`err = 0.000e+00`).

### `MAYA_DEVKIT_ROOT` resolution order

`FindMaya.cmake` searches for `maya/MFn.h` under the following paths
(first match wins):

1. `-DMAYA_DEVKIT_ROOT=<path>` — explicit cache variable.
2. `$MAYA_DEVKIT_ROOT` — exported environment variable.
3. `$MAYA_LOCATION` — Autodesk's native env var, usually the main
   Maya install.
4. `MAYA_DEVKIT_PROBE_PATHS` from `cmake/MayaVersionMatrix.cmake`
   (platform-specific defaults for the requested `MAYA_VERSION`).

Both of these root layouts are accepted:
- `<ROOT>/include/maya/MFn.h` (Maya's bundled headers)
- `<ROOT>/devkit/include/maya/MFn.h` (separate devkit download)

The Maya 2022 install ships headers in the first layout; the
standalone devkit archive ships them in the second.

## mayapy smoke test

After building the plugin, run the 4-step smoke contract
(`loadPlugin` → `createNode` → `setAttr` + assert → `unloadPlugin`):

```bash
"C:/Program Files/Autodesk/Maya2022/bin/mayapy.exe" \
    maya_node/tests/smoke/smoke_hellonode.py \
    build-maya-2022/bin/rbfmax_maya.mll
echo "exit code: $?"   # must be 0
```

Expected output (abridged):

```
[1/4] loadPlugin OK: build-maya-2022/bin/rbfmax_maya.mll
[2/4] createNode OK: mRBFNode1
[3/4] compute(1.0) = 0.36787944117144233 vs expected 0.36787944117144233 (err=0.000e+00) OK
[4/4] delete + unloadPlugin OK

=== Slice 10A mayapy smoke: PASS ===
```

The Python-side assertion uses a `1e-12` absolute tolerance (see
docstring rationale). The native-side adapter tests use `1e-14`.

## Usage (Slice 11)

The Slice 11 `mRBFNode` loads a trained RBF interpolator from a schema-v1
JSON file on disk and serves `predict()` to downstream plugs. Training
data is **not** a Maya attribute — train offline, save JSON, set the
node's `jsonPath`, and the node handles the rest. See the DEVLOG Slice 11
entry for the "no DG for training data" architecture rationale.

Python (`maya.cmds`) example:

```python
import maya.cmds as cmds

# Load plugin
cmds.loadPlugin("rbfmax_maya.mll")

# Create node and point it at a trained JSON file.
node = cmds.createNode("mRBFNode")
cmds.setAttr(f"{node}.jsonPath", "C:/path/to/rig.json", type="string")

# Inspect load state.
print(cmds.getAttr(f"{node}.isLoaded"))      # True
print(cmds.getAttr(f"{node}.nCenters"))      # e.g. 500
print(cmds.getAttr(f"{node}.dimInput"))      # e.g. 3
print(cmds.getAttr(f"{node}.dimOutput"))     # e.g. 1
print(cmds.getAttr(f"{node}.kernelType"))    # "Gaussian"

# Evaluate predict at a query point (double array).
cmds.setAttr(f"{node}.queryPoint",
             3, 0.1, 0.2, 0.3, type="doubleArray")
print(cmds.getAttr(f"{node}.outputValues"))  # [ ... M-vector ... ]

# Force a reload after the JSON file was edited on disk (path
# unchanged).
cmds.setAttr(f"{node}.reloadTrigger",
             cmds.getAttr(f"{node}.reloadTrigger") + 1)
```

### Attributes

| Attribute | Type | Direction | Purpose |
|-----------|------|-----------|---------|
| `jsonPath` | string | input | Path to a schema-v1 `.json` file produced by `RBFInterpolator::save` |
| `reloadTrigger` | int | input (keyable) | Bump to force re-read when path is unchanged but file content changed |
| `queryPoint` | doubleArray | input | N-dimensional query vector; length must match `dimInput` |
| `inputValue` | double | input | Legacy Slice 10A scalar — routes through `hello_transform` when `jsonPath` is empty |
| `outputValues` | doubleArray | output | Predict result (length = `dimOutput`) |
| `outputValue` | double | output | Legacy Slice 10A scalar output |
| `isLoaded` | bool | output | True after a successful load |
| `nCenters` | int | output | N from the loaded fit result |
| `dimInput` | int | output | D from the loaded fit result |
| `dimOutput` | int | output | M from the loaded fit result |
| `kernelType` | string | output | One of `Linear`, `Cubic`, `Quintic`, `ThinPlateSpline`, `Gaussian`, `InverseMultiquadric` |
| `statusMessage` | string | output | Human-readable last-load status ("OK" or a descriptive failure) |

### Failure modes (non-fatal)

`compute()` returns success on every failure path (never breaks DG
evaluation):

- **`jsonPath` empty or unreadable** → `isLoaded=false`, empty
  `outputValues`, descriptive `statusMessage`, single `MGlobal`
  warning per failing path.
- **`RBFInterpolator::load` returns false** (parse / schema / IO
  error) → same as above; root cause reported in `statusMessage`.
- **`queryPoint.length() != dimInput`** → empty `outputValues`;
  diagnose from the status outputs.

Warning deduplication is keyed on the current path value — a path
change or a `reloadTrigger` bump resets the "already warned" flag so
the new path gets its own single warning on failure.

### How to generate a training JSON

Slice 11 ships no in-Maya training command — that is slated for Slice
12 (`rbfmaxTrainAndSave` MEL / Python). Until then, options are:

1. **Standalone C++ util**: link `rbfmax::solver`, fit an
   `RBFInterpolator`, call `save("rig.json")`.
2. **Python binding**: Phase 2C will add a pybind11 wrapper. Until
   then, hand-write training data in a C++ harness.
3. **Copy-paste from `maya_node/tests/smoke/fixtures/tiny_rbf.json`**:
   the Slice 11 smoke fixture is a complete 4-corner example that can
   be edited manually; useful for experimentation but not a production
   workflow.

## Command: `rbfmaxTrainAndSave` (Slice 12)

MPxCommand that closes the "train from inside Maya" gap. Reads
training data from Python lists (inline mode) or CSV files, fits
a Phase 1 `RBFInterpolator`, and writes the schema-v1 JSON that
`mRBFNode` can load.

### Flags

| Long | Short | Type | Mode | Notes |
|------|-------|------|------|-------|
| `centers` | `c` | doubleArray | inline | Row-major flat list |
| `targets` | `t` | doubleArray | inline | Row-major flat list |
| `inputDim` | `idm` | int | inline | D = centers column count |
| `outputDim` | `od` | int | inline | M = targets column count |
| `centersFile` | `cf` | string | csv | Path to centers.csv (N × D) |
| `targetsFile` | `tf` | string | csv | Path to targets.csv (N × M) |
| `jsonPath` | `jp` | string | **required** | Output JSON path |
| `kernel` | `kn` | string | shared | `Gaussian` (default), `Cubic`, `Linear`, `Quintic`, `ThinPlateSpline`, `InverseMultiquadric` |
| `epsilon` | `ep` | double | shared | Shape parameter (default 1.0). Only used by Gaussian / IMQ |
| `polyDegree` | `pd` | int | shared | -1 = auto via `minimum_polynomial_degree`, 0..3 = explicit |
| `lambda` | `lm` | string | shared | `"auto"` (GCV, default) or numeric like `"1e-6"` |
| `force` | `fo` | bool | shared | Overwrite existing `jsonPath` (default false) |

> **Maya MSyntax quirk** — long flag names must be **≥ 4 characters** or
> `addFlag` silently drops them. This is why the lambda parameter is
> `-epsilon` (not `-eps`) and the input-dimension is `-inputDim`
> (not `-dim`). Caught as R-30 during Slice 12 smoke debugging.

### Example — inline mode

```python
import maya.cmds as cmds
cmds.loadPlugin("rbfmax_maya.mll")

out = cmds.rbfmaxTrainAndSave(
    centers=[0.0, 0.0,  1.0, 0.0,  0.0, 1.0,  1.0, 1.0],
    targets=[0.0, 1.0, 1.0, 2.0],
    inputDim=2, outputDim=1,
    jsonPath="C:/rigs/my_rbf.json",
    kernel="Gaussian", epsilon=1.0, polyDegree=-1,
    force=True,
    **{"lambda": "1e-6"})
print(out)   # "C:/rigs/my_rbf.json" on success
```

### Example — CSV mode

Contents of `centers.csv` (4 samples, D=2):

```
# optional '#' line comments and blank lines are tolerated
0.0,0.0
1.0,0.0
0.0,1.0
1.0,1.0
```

Contents of `targets.csv` (4 samples, M=1):

```
0.0
1.0
1.0
2.0
```

```python
cmds.rbfmaxTrainAndSave(
    centersFile="C:/data/centers.csv",
    targetsFile="C:/data/targets.csv",
    jsonPath="C:/rigs/my_rbf.json",
    kernel="Gaussian", epsilon=1.0,
    force=True,
    **{"lambda": "auto"})
```

### Typical workflow

```python
# 1. Train
cmds.rbfmaxTrainAndSave(centersFile=..., targetsFile=...,
                        jsonPath="rig.json", force=True)

# 2. Create node and point it at the freshly-trained file
node = cmds.createNode("mRBFNode")
cmds.setAttr(f"{node}.jsonPath", "rig.json", type="string")

# 3. Query at runtime
cmds.setAttr(f"{node}.queryPoint", [0.5, 0.5], type="doubleArray")
print(cmds.getAttr(f"{node}.outputValues"))
```

### Error behaviour

Failures raise `RuntimeError` from Python (`MS::kFailure` from
the underlying `MPxCommand`). Typical messages:

| Trigger | Message pattern |
|---------|-----------------|
| `-jsonPath` missing | `missing required flag -jsonPath / -jp` |
| Inline + CSV flags mixed | `modes are mutually exclusive` |
| Neither mode's flags supplied | `must supply either -centers and -targets ...` |
| Inline without `-inputDim` / `-outputDim` | `inline mode requires both -dim and -outputDim` |
| CSV parse failure | `centers csv parse failed: <detail>` |
| Unknown kernel string | `unknown kernel: "<x>"` |
| `jsonPath` exists and `-force` false | `file exists; pass -force true to overwrite: ...` |
| `lambda` unparseable | `cannot parse -lambda: "<x>"` |
| `solver::fit` failed | `fit failed: <FitStatus>` |
| `RBFInterpolator::save` failed | `save failed (could not write schema-v1 JSON to): ...` |

The command is **not undoable** — it writes a file rather than
modifying scene state. The `-force` flag is the only guard against
accidental overwrite.

## Viewport 2.0 visualization — `mRBFShape` (Slice 13)

Slice 13 introduces Viewport 2.0 rendering of a trained `mRBFNode`'s
centers.  Architecture (Path B): the compute node `mRBFNode` stays a
pure `kDependNode`; visualization is carried by a separate locator
node `mRBFShape` connected via a `message` attribute.  The draw
override is registered against `mRBFShape`'s classification.

### Attributes on `mRBFShape`

| Long name      | Short | Type    | Default | Notes                                                    |
|----------------|-------|---------|---------|----------------------------------------------------------|
| `sourceNode`   | `src` | message | —       | Connect from `mRBFNode.message`.                         |
| `drawEnabled`  | `de`  | bool    | `true`  | Per-shape toggle; when `false` the override draws nothing. |
| `sphereRadius` | `sr`  | double  | `0.05`  | Radius of each center marker; clamped [0.001, soft 1.0]. |

### Workflow

```python
import maya.cmds as cmds

cmds.loadPlugin("rbfmax_maya.mll")

# 1. Create the compute node and load a trained JSON (Slice 11 workflow).
rbf = cmds.createNode("mRBFNode")
cmds.setAttr(rbf + ".jsonPath", "C:/path/to/rig.json", type="string")

# 2. Create the shape and connect it.
shp = cmds.createNode("mRBFShape")
cmds.connectAttr(rbf + ".message", shp + ".sourceNode")

# 3. Switch the viewport to Viewport 2.0 — centers now render as
#    white filled spheres at the first-3-dim projection of each
#    fit-center row of the `mRBFNode`'s loaded interpolator.
#
# 4. (Optional) Per-shape tweaks:
cmds.setAttr(shp + ".sphereRadius", 0.1)
cmds.setAttr(shp + ".drawEnabled", False)  # temporarily hide
```

### Viewport behaviour notes

- **Dimension projection** — For `D > 3`, only the first 3 columns of
  `RBFInterpolator::centers()` are drawn.  For `D < 3`, missing
  coordinates are zero-padded.  This is an intentional simplification
  for Slice 13; a full DR projection mode is tracked as a Phase 2B
  follow-up.
- **Unloaded / broken connection states** — If `mRBFNode.jsonPath` is
  empty, or points to a missing or malformed file, `mRBFShape` shows
  nothing.  Same if `sourceNode` has no incoming connection.  No
  warning is emitted per draw; failures surface through
  `mRBFNode.statusMessage` / the Slice 11 one-shot `MGlobal::warning`.
- **Multiple shapes per node** — You can connect the same `mRBFNode`
  to several `mRBFShape` instances (different views, different LODs).
  Each shape has independent `drawEnabled` / `sphereRadius`.

### Heatmap mode (Slice 14 — HM-1)

`mRBFShape` has a `heatmapMode` enum attribute controlling how each
center is colored in the viewport:

| Mode             | Value | Slice 14 behavior                                                |
|------------------|-------|------------------------------------------------------------------|
| Off              | 0     | Uniform white spheres (Slice 13 default)                         |
| Center Weights   | 1     | Each center colored by its L2 weight norm via a viridis ramp     |
| Prediction Field | 2     | Reserved for Slice 15 — currently falls back to Off              |

```python
# Switch to weight-based heatmap.
cmds.setAttr(shp + ".heatmapMode", 1)

# Back to uniform white.
cmds.setAttr(shp + ".heatmapMode", 0)
```

Implementation notes:

- Color map is an 11-stop piecewise-linear viridis LUT
  (`color_mapping.cpp`).  Anchors at v=0 / v=0.5 / v=1 match
  matplotlib reference values within 1e-2 per channel.
- For each center i, `heat[i] = weights.row(i).norm()`; the heat
  vector is normalized to `[0, 1]` (per-frame `min`/`max` over finite
  rows), then mapped through the LUT.  Centers with `NaN`/`Inf`
  weights fall back to white.
- `mRBFDrawOverride::prepareForDraw` caches the per-center color
  vector keyed on the underlying weights buffer pointer + shape +
  mode.  Recomputation only happens when `fit()` / `load()` swaps
  the buffer, when the matrix is resized, or when the user switches
  the heatmap mode.

### Prediction Field (Slice 15 — HM-2)

When `heatmapMode = Prediction Field` (value 2), `mRBFShape` samples
the trained interpolator on a 2D grid in local space and renders each
sample point as a small viridis-colored sphere.  The trained centers
themselves remain drawn in white on top of the grid for visual contrast.

| Attribute | Short | Default | Range | Meaning |
|-----------|-------|---------|-------|---------|
| `gridResolution` | `gr` | `16` | `[2, 64]` | Points per side (G); total samples = G² |
| `gridExtent` | `ge` | `2.0` | `[0.01, ∞)` | Half-width of the XY sample area in local space |
| `gridZ` | `gz` | `0.0` | any | Z-plane height for `D ≥ 3` input dims |

```python
# Switch to prediction-field mode and tune the grid.
cmds.setAttr(shp + ".heatmapMode", 2)        # 2 = Prediction Field
cmds.setAttr(shp + ".gridResolution", 32)    # ~1024 sample points
cmds.setAttr(shp + ".gridExtent", 3.0)       # covers [-3, +3] x [-3, +3]
cmds.setAttr(shp + ".gridZ", 0.5)            # only matters if D >= 3
```

Dimension handling for the input dim `D` of the loaded interpolator:

- `D == 1` — only the `gx` coordinate of each sample is used; `gy`
  and `gridZ` discarded.  Layout is still G² points (a 1D probe
  laid out on a square just for visualization parity).
- `D == 2` — `(gx, gy)` per sample.  This is the natural case for
  most rig poses.
- `D == 3` — `(gx, gy, gridZ)` per sample.  Move `gridZ` to slice
  the field at different heights.
- `D ≥ 4` — `(gx, gy, gridZ, 0, 0, ...)`.  Higher dimensions get
  zero-filled; configure them via the upstream rig if needed.

`mRBFDrawOverride::prepareForDraw` caches the grid colors keyed on
the weights buffer pointer plus `(gridResolution, gridExtent, gridZ)`.
`predict_batch` only fires when one of those changes or when the
mode itself flips.

### X-Ray mode (Slice 15 — XR-1)

```python
cmds.setAttr(shp + ".xrayMode", True)   # raise depth priority
cmds.setAttr(shp + ".xrayMode", False)  # back to default
```

When `xrayMode = True`, both centers and the prediction-field grid
are drawn with a higher Viewport 2.0 depth priority (raw 10 vs the
Slice 13 default 5), so they are not occluded by surrounding scene
geometry.  This is most useful when previewing a rig embedded in
a character mesh.

Maya 2022 / 2025 expose only the raw integer overload of
`MUIDrawManager::setDepthPriority`; there is no `DepthPriority`
enum on that class in either version, so the implementation uses
literal integers (5 and 10) with comments explaining the choice.

## Known limitations (Slice 10A)

- **Development-range typeId**: `0x00013A00`. This ID is valid for
  internal development only and **must not be distributed**. Registering
  a production plugin requires requesting a permanent block from
  Autodesk (`typeId` registration). Logged as tech-debt item **T-10**
  in `DEVLOG.md`.
- **No strict warnings on the plugin target**: `rbfmax_apply_warnings()`
  is deliberately not applied to `rbfmax_maya_node` because MSVC `/WX`
  still bites on some Maya macro expansions even when headers are
  SYSTEM-included. Slice 10B+ will reintroduce strict warnings behind
  `/external:W0` (MSVC ≥ 16.10) or `-isystem` on Clang/GCC. Adapter
  tests do honour the strict warning set.
- **Dynamic array attributes** added in Slice 11 (`queryPoint`,
  `outputValues` as `MFnDoubleArrayData`) — variable-dim queries
  are supported.
- **Maya 2024 / 2026 validation** still pending — devkits not yet
  on the development machine. Slice 10C (Maya 2025) and the double-
  environment smoke infrastructure are already in place.

## Files

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Plugin target + adapter test gate |
| `include/rbfmax/maya/adapter_core.hpp` | Pure-C++, C++14-compliant helpers (Gaussian eval, attribute marshalling, CSV/lambda parsing) |
| `include/rbfmax/maya/mrbf_node.hpp` | `MPxNode` declaration — JSON-path load + predict + Slice 13 additive getters |
| `include/rbfmax/maya/mrbf_shape.hpp` | `MPxLocatorNode` declaration — Slice 13 Viewport 2.0 host |
| `include/rbfmax/maya/mrbf_draw_override.hpp` | `MPxDrawOverride` declaration — Slice 13 |
| `include/rbfmax/maya/draw_sink.hpp` | Maya-free `IDrawSink` abstraction — Slice 13 (Slice 14 heatmap consumer) |
| `include/rbfmax/maya/rbfmax_train_cmd.hpp` | `MPxCommand` declaration for `rbfmaxTrainAndSave` |
| `include/rbfmax/maya/plugin_info.hpp.in` | CMake-configured constants (version, typeId) |
| `src/mrbf_node.cpp` | `compute()` + `try_load()` + attribute wiring + Slice 13 accessors |
| `src/mrbf_shape.cpp` | Slice 13 Path B locator implementation |
| `src/mrbf_draw_override.cpp` | Slice 13 Viewport 2.0 `prepareForDraw` + `addUIDrawables` |
| `src/draw_sink_core.cpp` | Slice 13 `emit_centers_draw_calls` — orchestration for Slice 14+ |
| `src/plugin_main.cpp` | `initializePlugin` / `uninitializePlugin` — registers node + command + shape + draw override |
| `src/rbfmax_train_cmd.cpp` | `rbfmaxTrainAndSave` `doIt()` implementation |
| `src/adapter_core_csv.cpp` | Non-inline CSV / lambda parser implementations |
| `tests/CMakeLists.txt` | Adapter test target (links `adapter_core_csv.cpp` + `draw_sink_core.cpp`) |
| `tests/test_adapter_core.cpp` | 17 GTest blocks: H1-H3 (Slice 10A) + C1-C6 (Slice 11) + D1-D8 (Slice 12) |
| `tests/test_draw_sink.cpp` | 8 GTest blocks: E1-E8 (Slice 13 IDrawSink contract) |
| `tests/smoke/smoke_hellonode.py` | Slice 10A `mayapy` 4-step contract |
| `tests/smoke/smoke_predict.py` | Slice 11 `mayapy` 5-step contract (load + predict bit-identity) |
| `tests/smoke/smoke_train.py` | Slice 12 `mayapy` 4-scenario contract (csv / inline / force / bad-kernel) |
| `tests/smoke/smoke_viewport.py` | Slice 13 `mayapy` 7-step contract (load + classification + connect + predict + shape attrs) |
| `tests/smoke/fixtures/tiny_rbf.json` | Slice 11 Phase 1-generated schema-v1 fixture |
| `tests/smoke/fixtures/tiny_rbf_expected.json` | Slice 11 reference predict outputs |
| `tests/smoke/fixtures/tiny_train_centers.csv` | Slice 12 CSV fixture matching tiny_rbf's centers |
| `tests/smoke/fixtures/tiny_train_targets.csv` | Slice 12 CSV fixture matching tiny_rbf's targets |
