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
- **No dynamic array attributes yet** — Slice 11 adds per-sample centers
  and targets as Maya array attributes.
- **No Maya 2024/2025/2026 validation** — see the version table above.

## Files

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Plugin target + adapter test gate |
| `include/rbfmax/maya/adapter_core.hpp` | Pure-C++, C++14-compliant, calls Phase 1 `evaluate_kernel` |
| `include/rbfmax/maya/mrbf_node.hpp` | `MPxNode` skeleton class declaration |
| `include/rbfmax/maya/plugin_info.hpp.in` | CMake-configured constants (version, typeId) |
| `src/mrbf_node.cpp` | `compute()` + attribute wiring |
| `src/plugin_main.cpp` | `initializePlugin` / `uninitializePlugin` |
| `tests/CMakeLists.txt` | Adapter test target |
| `tests/test_adapter_core.cpp` | 3 TEST blocks (H1/H2/H3) |
| `tests/smoke/smoke_hellonode.py` | `mayapy` 4-step contract |
