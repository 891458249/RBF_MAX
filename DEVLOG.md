# Dev Log — RBF_MAX

面向**开发**的工程日志。记录每个切片（Slice）级别的技术决策、风险、遗留问题。
对外可见的功能变更请查阅 [`CHANGELOG.md`](./CHANGELOG.md)。

---

## 日志约定

- **每个切片**（一次 `git commit` 涵盖的一组逻辑相关文件）**必须**写一条条目。
- 条目按时间倒序排列（最新在最上）。
- 每条包含：标题、提交哈希、阶段/切片编号、决策、风险/TODO、验收门、文件变更摘要。
- 提交哈希在 commit 创建后回填，可用 `scripts/update_devlog_hash.sh`（阶段二提供）。

---

## 2026-04-21 · Slice 11 — mRBFNode real predict via JSON-path load (Phase 2A core)

**Scope**: Phase 2A core functional slice. `mRBFNode` graduates from the Slice 10A HelloNode skeleton to a real predictor — it loads a Phase 1 `RBFInterpolator` from a schema-v1 JSON file and serves `predict()` to downstream plugs. First slice where Phase 1's kernel and solver both run inside a Maya plugin. Double-validated on Maya 2022 + Maya 2025 on first try (Slices 10A/10C investment pays off).

**Deliverables**
- `kernel/include/rbfmax/interpolator.hpp` + `.cpp` + `tests/test_interpolator.cpp` — additive `kernel_params() const noexcept` getter + `RBFInterpolatorState.KernelParamsReflectsFit` test. 3 lines of Phase 1 public surface, noexcept/Maya-free/engine-agnostic.
- `maya_node/CMakeLists.txt` — plugin now links `rbfmax::solver` (first consumer of the STATIC lib from inside Maya). Plugin version string bumped `1.0.0-phase2a-slice10a → 1.0.0-phase2a-slice11`.
- `maya_node/include/rbfmax/maya/adapter_core.hpp` — 3 new helpers (`double_vector_to_eigen`, `eigen_to_double_vector`, `validate_json_path`), all C++14-compliant.
- `maya_node/include/rbfmax/maya/mrbf_node.hpp` + `src/mrbf_node.cpp` — expanded to ~12 attributes (8 inputs / 4 outputs) plus `try_load` helper; ~420 LOC of real compute logic.
- `maya_node/tests/test_adapter_core.cpp` — 6 new C-group tests (C1–C6).
- `maya_node/tests/smoke/smoke_predict.py` — 5-step mayapy contract exercising loadPlugin → state inspection → three real predicts with bit-identity assertions → cleanup.
- `maya_node/tests/smoke/fixtures/{tiny_rbf.json, tiny_rbf_expected.json}` — out-of-repo-generated fixture (see §Fixture reproducibility below).
- `maya_node/README.md` — Usage section with Python example, full attribute table, failure-mode catalogue.

**Core architecture decision — "training data does not cross DG"**

Training matrices (centers, targets) are NOT Maya attributes. The user trains offline (future `rbfmaxTrainAndSave` command / Python binding / C++ harness), saves schema-v1 JSON, and the node reads it. Rationale: Maya DG dirty tracking over an N×D compound array attribute is far more expensive than one file read at load time; Slice 08's schema-v1 is already the canonical on-disk representation; professional Maya RBF systems (Maya Muscle, facial / AR rigs) all follow this pattern. Keeps node responsibilities clean: predictor + config container.

**15 locked design decisions**
- **A0** `mRBFNode` / typeId `0x00013A00` — both inherited from Slice 10A.
- **A1** Training data source: `jsonPath` string attribute + `MFnStringData`.
- **A2** `aQueryPoint` = typed MFnDoubleArrayData (variable D).
- **A3** `aOutputValues` = typed MFnDoubleArrayData (variable M).
- **A4** 6 state-output attributes: `isLoaded` / `nCenters` / `dimInput` / `dimOutput` / `kernelType` / `statusMessage`, all readable-only non-storable.
- **B1** Load triggered by `jsonPath` change OR `reloadTrigger` bump; both in attributeAffects chain.
- **B2** Load failure → full reset (interp_ = nullptr, outputs zero/empty, statusMessage populated).
- **B3** Lazy load on first compute that sees a non-empty, changed path.
- **C1** `std::unique_ptr<RBFInterpolator> interp_` matches the move-only contract.
- **C2** No node-side pool management — `RBFInterpolator` already owns its ScratchPool (Slice 06/07).
- **C3** kdtree threshold is a Phase 1 default (256); not exposed as a node attribute.
- **D1** `compute()` always returns kSuccess on JSON-path paths. Failures surface via `statusMessage` + `isLoaded=false` + empty `outputValues`.
- **D2** Single `MGlobal::displayWarning` per failing path (dedup via `warned_about_current_path_`, reset on path or reloadTrigger change).
- **E1** Scheduling `kNormal` (MPxNode default). RBFInterpolator is non-thread-safe; Phase 2 may upgrade with clone()-per-thread.
- **F1** `adapter_core.hpp` extended with 3 pure-C++ helpers that the GTest suite can cover without Maya runtime.

**Spec-drift catches (4 pre-write, 1 mid-execution)**

Reviewer channel caught four drifts during pre-flight Phase 1 API grep:
- **G1** Smoke assertion string `"kGaussian"` → `"Gaussian"` (`kernel_type_to_string` strips the `k` enumerator prefix). Grep'd `kernel_functions.hpp:237`.
- **G2/G3** No public kernel-type getter existed on `RBFInterpolator`. The Section G prohibition "❌ 改 Phase 1 ... 任何代码" internally contradicted the `aKernelType` requirement in the same spec. Reviewer evaluated paths A (node-side JSON re-parse) / B (additive getter on RBFInterpolator) / C (drop `aKernelType`) and chose **B** despite executor's initial A-leaning recommendation. Rationale: A transfers an encapsulation gap to every future consumer (Phase 2C UI, external C++ bindings, cross-DCC); B is 3 LOC noexcept/Maya-free additive code with zero behavioural risk. The Section G prohibition was amended from "any Phase 1 code" to "any Phase 1 behavioural code" with explicit allowance for additive const getters + tests — documented here as precedent for future Phase 2 slices.
- **G4** spec §A5 included redundant `set_property(TARGET rbfmax_solver PROPERTY POSITION_INDEPENDENT_CODE ON)` — `CMakeLists.txt:36` already sets `CMAKE_POSITION_INDEPENDENT_CODE ON` globally. Dropped.

Executor caught one more drift during the first predict-smoke run:
- **F4** `cmds.setAttr("x.foo", count, v0, v1, type="doubleArray")` with unpacked count + values silently truncated to a 1-element array in both Maya 2022 and 2025. Correct invocation is `cmds.setAttr("x.foo", [v0, v1], type="doubleArray")` (pass a Python list, length is implicit). Fixed in `smoke_predict.py` before the final run. The symptom was `getAttr("...outputValues")` returning `None` — because our `compute()` saw `queryArr.length() == 1 != dim() == 2` and wrote an empty `MDoubleArray`. Investigation order: added defensive probe script, printed `queryPoint` readback `[2.0]` instead of `[0.5, 0.5]`, traced back to the setAttr form.

**Tolerance register**
- `RBFInterpolatorState.KernelParamsReflectsFit` — `EXPECT_DOUBLE_EQ(1.0, 1.0)` exact; `EXPECT_EQ` on enum. Double literal `1.0` is bit-identical through construction → FitResult → getter; zero wiggle room needed.
- adapter C1–C6 round-trip — `EXPECT_DOUBLE_EQ` / exact `==`. Memcpy-equivalent code path; err=0 observed in all cases.
- smoke_predict bit-identity — `1e-10` absolute; observed `err=0` on all 3 queries on both Maya 2022 and 2025. The tolerance is defence in depth against unknown DG internal double round-trips; empirically unused.

**Fixture reproducibility (committed-to-DEVLOG record)**

`maya_node/tests/smoke/fixtures/{tiny_rbf.json, tiny_rbf_expected.json}` were generated ONCE out-of-repo by a standalone C++ util. The util is NOT committed (not project source), but its content is recorded here for full auditability:

```cpp
// scripts/generate_tiny_rbf.cpp — Slice 11 fixture generator
// Build against Phase 1 rbfmax::solver in Release, run once.
//
// Usage:
//   generate_tiny_rbf <tiny_rbf.json> <tiny_rbf_expected.json>
//
// Fits a 4-corner 2D Gaussian RBF (N=4, D=2, M=1, eps=1,
// poly_degree=-1, lambda=1e-6, target=x+y), calls rbf.save() for
// tiny_rbf.json, then calls predict on three queries and writes
// results to tiny_rbf_expected.json.
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <nlohmann/json.hpp>
#include <rbfmax/interpolator.hpp>
#include <rbfmax/kernel_functions.hpp>
#include <rbfmax/types.hpp>

int main(int argc, char** argv) {
    using namespace rbfmax;
    MatrixX C(4, 2);  C << 0,0, 1,0, 0,1, 1,1;
    MatrixX T(4, 1);  T << 0, 1, 1, 2;
    InterpolatorOptions opts(KernelParams(KernelType::kGaussian, 1.0));
    opts.poly_degree = -1;
    RBFInterpolator rbf(opts);
    rbf.fit(C, T, 1e-6);
    rbf.save(argv[1]);

    struct Q { double x, y; };
    const std::vector<Q> queries = { {0,0}, {0.5,0.5}, {2,2} };
    nlohmann::json out;
    out["description"] = "Slice 11 smoke fixture ...";
    for (const auto& q : queries) {
        VectorX x(2); x << q.x, q.y;
        VectorX y = rbf.predict(x);
        out["queries"].push_back({{"query", {q.x, q.y}},
                                   {"expected", {y(0)}}});
    }
    std::ofstream(argv[2]) << out.dump(2);
    return 0;
}
```

The reference outputs ship inside `tiny_rbf_expected.json`:
- `query=[0.0, 0.0]` → `expected=[6.220699456105372e-07]` — note this is NOT zero. Tikhonov λ=1e-6 smooths sample-point reconstruction by exactly this amount; Phase 1's Slice 05 G1 reconstruction test uses a 0.1 RMSE tolerance on random samples precisely because this smoothing is expected.
- `query=[0.5, 0.5]` → `expected=[1.2966324126539757]` — interpolation overshoots the linear target `x+y=1.0` because Gaussian ε=1 is not a linear basis.
- `query=[2.0, 2.0]` → `expected=[0.23584037247987003]` — far-field decay.

**Phase 1 amendment (scope exception)**

Section G's original "不改 Phase 1 任何代码" was amended to "不改 Phase 1 任何行为性代码" with explicit allowance for additive const getters accompanied by tests. `RBFInterpolator::kernel_params() const noexcept` lands under this allowance, along with `RBFInterpolatorState.KernelParamsReflectsFit`. No behavioural change to fit / predict / save / load / clone. Precedent documented here for future Phase 2 slices encountering similar Phase 1 surface gaps.

**Tech-debt register additions**
- **R-25** `MFnDoubleArrayData` round-trip across Maya versions — validated identical on 2022 + 2025. Closed.
- **R-26** Lazy-load I/O pattern — implemented: load only on path change or reloadTrigger bump, not per-frame. Closed.
- **R-27** unique_ptr<RBFInterpolator> + move semantics + kdtree/ScratchPool invariants — validated end-to-end through fresh/reset/reload cycles in smoke_predict. Closed.
- **R-28** JSON path unicode / backslash escape — `validate_json_path` uses `std::ifstream` so C++ runtime handles this. Windows + Linux consistent. Closed.
- **R-29** `cmds.setAttr` for `doubleArray`: unpacked `count,v0,v1,…` form silently truncates to 1 element — must use Python list form. Documented in smoke script comments and `maya_node/README.md` Usage section. **Open** as a living Maya API cookbook note for future Phase 2 scripts.
- **T-11** Slice 11 ships no `save` API on the node — users must train offline. Deferred to Slice 12 (`rbfmaxTrainAndSave` command). **Open**.
- **T-12** Phase 1 API amendment (kernel_params getter) — 1.1.0 bump target still Phase 2A end. **Open**.

**Validation outcomes** (Windows 11, MSVC 19.44.35223)

| Step | Command summary | Result |
|------|-----------------|--------|
| 1 | `build-adapter` Release with `RBF_BUILD_MAYA_ADAPTER_TESTS=ON` | **146/146 green**, 12.56 s (137 Phase 1 + 3 H + 6 C) |
| 2a | Maya 2022 plugin build | 0 warn 0 err, **158 208 bytes** |
| 2b | Maya 2025 plugin build | 0 warn 0 err, **158 208 bytes** (byte-identical to 2022 — source-level code is ABI-agnostic and both toolchains produce equivalent object code from it) |
| 3a | Maya 2022: hellonode + predict smokes | both **exit 0**; all 3 predict queries err=0 exactly (well below 1e-10 tolerance); all 6 state attributes report correctly |
| 3b | Maya 2025: hellonode + predict smokes | both **exit 0**; values bit-identical to 2022 — Phase 2A version-matrix decoupling validated in its first real business-logic test |
| 4 | Phase 1 Release regression | **137/137 green**, 10.20 s (unchanged from v1.0.0 + kernel_params test) |

The double-environment validation (Step 3a vs 3b bit-identity) is the single most valuable signal this slice produces: it confirms the Slice 10A/10C "shift-left + version-agnostic" design carries through to real business logic, so Phase 2 slices from here on get "code bug vs version bug" disambiguation for free.

**Workflow note**
- Branch `slice-11-mrbfnode-predict` → 4 commits (feat(kernel) / build(cmake) / feat(maya) / docs(devlog)) → PR → CI 3 Phase 1 jobs (Maya opts default OFF) → human approve → rebase merge → auto-delete.
- No tag, no version bump (D14). `v1.1.0` is still the Phase 2A-end target.

**Outstanding after Slice 11**
- **Slice 12** — `rbfmaxTrainAndSave` MEL/Python command: closes T-11 so users can train inside Maya. Likely also where v1.1.0 ships.
- **Slice 10B** — Maya 2024 validation. Non-blocking.
- **Slice 10D** — Maya 2026 validation. Non-blocking.
- **Phase 2B / 2C** — Viewport 2.0 draw override, Qt6 UI. Not before v1.1.0.

---

## 2026-04-21 · Slice 10C — Maya 2025 devkit validation

**Scope**: Phase 2A validation slice. Activates the Maya 2025 branch of `cmake/MayaVersionMatrix.cmake` (`MAYA_CXX_STD=17`) and validates that the Slice 10A build chain works unchanged against the Maya 2025 devkit + Python 3.11 mayapy. Second of 4 Phase 2A version-matrix slices: **10A = 2022 ✅, 10C = 2025 ✅**, 10B = 2024 / 10D = 2026 still pending.

**Deliverables**
- `maya_node/README.md` — new "Build — Maya 2025" section + status table update to reflect 10A/10C both validated.
- `DEVLOG.md` — this entry.
- (No source code changes; 10A abstractions held up verbatim.)

**Environment setup** (out-of-repo, recorded for audit)
- Maya 2025 devkit relocated from `C:/Users/Administrator/Downloads/Autodesk_Maya_2025_3_Update_DEVKIT_Windows/devkitBase` to the canonical path `C:/SDK/Maya2025/devkitBase`. Same-drive `mv` — instant, no copy. The new path is space-free, Downloads-cleanup-proof, and parallels the future `C:/SDK/Maya2024` / `C:/SDK/Maya2026` layout.
- Maya 2025 runtime: `C:/Program Files/Autodesk/Maya2025/bin/mayapy.exe` with embedded Python **3.11.4** (vs Maya 2022's 3.7).

**Design decisions (4 delta vs Slice 10A)**
1. **10C-Δ1 — Activate `MAYA_CXX_STD=17` branch.** First real exercise of `MayaVersionMatrix.cmake`'s else-arm. `adapter_core.hpp` was deliberately written C++14-compatible in 10A, so no header code change was required. Confirmed at configure time: `Maya target version: 2025 (C++17)`.
2. **10C-Δ2 — Python 3.11 mayapy.** Smoke script uses only Python 3.6-stable features (`from __future__ import print_function`, `.format()`, `os.path`, `maya.standalone`, `maya.cmds`, `sys.exit`). Zero adjustment.
3. **10C-Δ3 — Maya 2025 inline namespace (`Autodesk::Maya::OpenMaya20250000`).** No impact: Slice 10A's F2 fix (dropping `extern "C"` in `plugin_main.cpp`) is version-agnostic because the trigger is the inline-namespaced return type, which every Maya 2022+ ABI has regardless of year.
4. **10C-Δ4 — Devkit path convention.** Canonicalised to `C:/SDK/Maya2025/devkitBase` to stabilise against Downloads cleanup and match future 10B/10D layout. `MAYA_DEVKIT_PROBE_PATHS` in `MayaVersionMatrix.cmake` already contains `C:/Autodesk/Maya2025/devkit` but not `C:/SDK/...`; we rely on the higher-priority `-DMAYA_DEVKIT_ROOT=...` override rather than probing. Probe-path expansion for `C:/SDK` is deferred to Slice 10B/10D (when 2024/2026 devkits land in the same tree).

**Validation outcomes** (Windows 11, MSVC 19.44.35223)
- **Step 1** — `build-adapter` Release with `RBF_BUILD_MAYA_ADAPTER_TESTS=ON`: **139/139 green**, 11.60s. Phase 1 136 + HelloTransform H1/H2/H3. 2 by-design skips. Confirms Slice 10A basework survived main's post-10A advance (`9169772 → 859004d`).
- **Step 2** — `build-maya-2025` Release with `RBF_BUILD_MAYA_NODE=ON MAYA_VERSION=2025 MAYA_DEVKIT_ROOT=C:/SDK/Maya2025/devkitBase`: `rbfmax_maya.mll` linked clean (**0 warnings, 0 errors**). 25,088 bytes — byte-identical size to the Maya 2022 output, which is expected: our source-level code is ABI-agnostic and the two devkits ship equivalent import libs for the symbols we reference.
- **Step 3** — `mayapy smoke_hellonode.py …/rbfmax_maya.mll` with Maya 2025's `mayapy.exe` (Python 3.11.4): **exit 0**. 4/4 contract steps. `compute(1.0) = 0.36787944117144233` matches `exp(-1)` bit-identically (`err = 0.000e+00`). Unrelated Quixel MSLiveLink `userSetup.py` noise printed at startup (same as Slice 10A); does not affect exit code.
- **Step 4** — `build` Release Phase 1 regression: **136/136 green**, 10.46s.

**Tech-debt register**
- **None new.** Phase 1 + 10A + 10C are all clean.
- Note for 10B/10D: the `MAYA_DEVKIT_PROBE_PATHS` list in `cmake/MayaVersionMatrix.cmake` does not yet include `C:/SDK/Maya<ver>/devkitBase`. We preferred explicit `-DMAYA_DEVKIT_ROOT` over adding probe paths in 10C because (a) the `SDK/` convention is a workstation choice not a universal default and (b) probe expansion should happen when there is a concrete second example (10B or 10D) to avoid baking one user's layout into the shared defaults prematurely.

**Cost-benefit realised**
Slice 10C was the "low-LOC high-audit-value" slice the pre-slice strategic argument predicted. Zero source code changes, zero spec drift, zero in-flight fixes. The 10C-Δ1 through 10C-Δ4 list is an *accounting* exercise — each Δ was present in 10A's design but untested until 10C ran. Total executor time dominated by the four CMake/build/ctest/smoke commands, each of which ran first-try.

**Workflow note**
- Branch `slice-10c-maya2025-validation` → PR → CI (Phase 1 3 jobs; Maya options default OFF so CI remains green without Maya on the runners) → human approve → rebase merge → auto-delete head branch (T-09 continuing to hold).
- No tag, no version bump (per 10A's D14).
- Single commit sufficient: this slice is doc + validation only.

**Outstanding after Slice 10C**
- **Slice 10B** — Maya 2024 validation. Requires a Maya 2024 install + devkit; not on the dev machine currently. Non-blocking.
- **Slice 10D** — Maya 2026 validation. Same prerequisite. Non-blocking.
- **Slice 11** — mRBFNode real kernel integration (dynamic array attributes for centers/targets, `fit()` trigger, `predict()` in `compute()`, error propagation). Evaluated as Phase 2A's heaviest slice (18–22 decision points, 700–1200 LOC). Can proceed immediately now that the version matrix is 2/4 validated — Slice 11 will validate against both Maya 2022 and Maya 2025 in one shot thanks to 10C.

---

## 2026-04-21 · Slice 10A — Maya devkit integration + mRBFNode skeleton (Phase 2A foundation)

**Scope**: First Phase 2 slice. Establish the CMake / FindMaya toolchain, land a minimal `mRBFNode` that links the Phase 1 kernel into a Maya plugin, and prove the pipeline end-to-end via `mayapy` smoke. **Validation-only** — no RBF fit/predict exposure yet (that's Slice 11). No version bump; no tag.

**Deliverables**
- `cmake/MayaVersionMatrix.cmake` — maps `MAYA_VERSION` ∈ {2022, 2024, 2025, 2026} to `MAYA_CXX_STD` (14 for 2022, 17 for the rest) and defines `MAYA_DEVKIT_PROBE_PATHS` per platform.
- `cmake/FindMaya.cmake` — handwritten module with explicit `MAYA_DEVKIT_ROOT` → `$ENV{MAYA_DEVKIT_ROOT}` → `$ENV{MAYA_LOCATION}` → probe path resolution. Exports `Maya::OpenMaya` / `Maya::OpenMayaAnim` / `Maya::Foundation` IMPORTED targets + aggregate `Maya::Maya`. Headers are SYSTEM-included.
- `CMakeLists.txt` (top-level) — two new opt-in `BOOL` options: `RBF_BUILD_MAYA_NODE` and `RBF_BUILD_MAYA_ADAPTER_TESTS`. Both OFF by default so Phase 1 builds and existing CI remain untouched.
- `maya_node/` subtree: CMake target `rbfmax_maya_node` (MODULE, `.mll` / `.so` / `.bundle`), adapter-core header, node skeleton (`mRBFNode`), generated `plugin_info.hpp`, 3 adapter tests, `mayapy` smoke script, subtree-local README.
- DEVLOG entry (this entry).

**Design decisions (15 locked pre-slice, D1–D15)**
1. **D1 Devkit path management** — explicit `-DMAYA_DEVKIT_ROOT` wins; falls back through `$MAYA_DEVKIT_ROOT`, `$MAYA_LOCATION`, then `MAYA_DEVKIT_PROBE_PATHS`.
2. **D2 FindMaya source** — handwritten (in-house owned) rather than third-party.
3. **D3' Maya version anchor** — Maya 2022 (re-anchored from 2025 after pre-flight probe; see "Pre-execution environment probe & corrections" below).
4. **D4' C++ standard** — `cxx_std_14` on the plugin target, `cxx_std_17` on the adapter tests. `adapter_core.hpp` kept C++14-compliant so it compiles under either.
5. **D5 Directory layout** — `maya_node/{include,src,tests}/` mirrors the Phase 1 `kernel/` layout.
6. **D6 CMake options** — two independent BOOLs rather than a single tri-state.
7. **D7 compute() behaviour** — `gaussian(|x|, eps=1.0)`, so the smoke test exercises a real kernel call rather than a synthetic identity.
8. **D8 typeId** — `0x00013A00` (= 80384, mid-range of the Autodesk dev range [0, 0x7FFFF]). The spec's initial draft `0x0013AB00` (= 1,288,960) was R-09-rejected before dispatch for exceeding the range.
9. **D9 Test layering** — GoogleTest for native adapter, `mayapy` for the end-to-end contract; no redundancy.
10. **D10 mayapy smoke contract** — 4 steps (loadPlugin / createNode / setAttr+getAttr+assert / delete+unloadPlugin).
11. **D11 kernel linkage** — plugin links `rbfmax::kernel` only (header-only), **not** `rbfmax::solver`. Slice 10A does not need solver symbols and keeping them out of the plugin's link graph keeps the Phase 2A foundation smaller.
12. **D12 Platform suffix** — `.mll` / `.bundle` / (empty → `.so`) set explicitly via target properties.
13. **D13 Warning strategy** — Maya headers get `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` in `FindMaya.cmake`. `rbfmax_apply_warnings()` is deliberately NOT applied to the plugin target (deferred to Slice 10B+ behind `/external:W0`). Adapter tests DO honour the strict warning set.
14. **D14 Version** — no bump, no tag. Project stays at 1.0.0 until Slice 11 lands real user-facing functionality.
15. **D15 Random seed** — `0xF5BFA9u` reserved in `test_adapter_core.cpp` for future randomised adapter tests (Slice 11+). H1-H3 are deterministic and do not consume it.

**Pre-execution environment probe & corrections**

Two Section VI stop conditions fired during executor pre-flight check (slice proposed with Maya 2025 anchor; no code landed):

1. **Maya 2025 devkit absent** — only Maya 2022 devkit present locally. Reviewer channel re-anchored D3 to Maya 2022 under shift-left validation principle (tightest C++ standard first). Phase 2A slice ordering revised to 10A=2022 → 10B=2024 → 10C=2025 → 10D=2026.
2. **Spec API signature wrong** — `adapter_core.hpp` initial draft called `rbfmax::kernel_functions::evaluate(type, eps, r)`; actual Phase 1 public API is `rbfmax::evaluate_kernel(type, r, eps)` (flat namespace, r-before-eps). Verified against `kernel_functions.hpp:195` before any file was written.

Additionally caught during spec drafting by R-09 self-check (reviewer channel):
3. **typeId initial draft `0x0013AB00` exceeds Autodesk development range [0, 0x7FFFF]** — corrected to `0x00013A00` (= 80384, safely in mid-range) before dispatch.

Phase 1 Retrospective flagged 5 prior spec-vs-code drifts; Slice 10A contributes items 6 and 7 to that list. New Phase 2 reviewer constraint: **every Phase 1 API reference in a spec must be grep-verified against the current kernel headers before dispatch**.

**Additional executor observation (not a spec error, but notable)**

Maya 2022's `<install>/devkit/` directory only contains a README pointing to a separate devkit download; the headers that ship with the main install live at `<install>/include/maya/` and import libs at `<install>/lib/`. `FindMaya.cmake` was therefore written to accept either layout via `PATH_SUFFIXES include devkit/include` and the parallel lib suffixes. Using `-DMAYA_DEVKIT_ROOT="C:/Program Files/Autodesk/Maya2022"` (the main install path, not a `/devkit` subpath) is the working invocation on this machine.

**Tolerance register (R-09 audit)**
- H1 `hello_transform(0) == 1.0` — exact equality; `exp(0) = 1.0` under IEEE 754.
- H2 `hello_transform(1) ≈ exp(-1)` — `1e-14` absolute. Machine ε ≈ 2.22e-16; single `std::exp` ≤ 1-2 ULP ≈ 5e-16. `1e-14` leaves ~45× safety margin.
- H3 `hello_transform(-x) == hello_transform(x)` — exact equality; `std::abs` is bit-exact, downstream arithmetic identical.
- mayapy smoke assertion — `1e-12` absolute. Maya's DG marshals doubles bit-identically in theory; `1e-12` is the conservative upper bound against any internal formatting drift.

**Tech-debt register additions**
- **R-17** Maya 2022 ABI strict-ness (VS2019 vs VS2022 CRT mixing) — deferred to Slice 10D if it surfaces.
- **R-18** `mayapy` path differences on Linux / macOS — deferred to Slice 10B/C/D environment-specific validation.
- **R-19** `NOMINMAX` requirement on Windows (Maya `min`/`max` macros collide with Eigen / `std::min`) — **closed in Slice 10A** by setting `NOMINMAX` + `_CRT_SECURE_NO_WARNINGS` + `REQUIRE_IOSTREAM` on the plugin target.
- **R-20** Handwritten `FindMaya.cmake` — **closed in Slice 10A**.
- **R-21** Devkit path resolution priority — **closed in Slice 10A** (priority chain implemented in `FindMaya.cmake`).
- **T-10** Development-range typeId (`0x00013A00`) is not safe for distribution; must request a permanent block from Autodesk before any public release beyond internal development.

**Workflow note**
- Branch: `slice-10a-maya-skeleton` → PR → CI (Phase 1 matrix only; Maya options default OFF so no CI change is needed) → human approve → rebase merge → auto-delete head branch.
- No CHANGELOG change (no user-facing behaviour). No top-level README change (Phase 2 will update it when there is real user-facing behaviour to describe).
- `.github/workflows/ci.yml` intentionally not modified: adapter-tests stay opt-in until Phase 2A stabilises.

**Local verification** (Windows 11, MSVC 19.44.35223, Maya 2022 `C:/Program Files/Autodesk/Maya2022`)
- Step 1 — `build-adapter` Release with `RBF_BUILD_MAYA_ADAPTER_TESTS=ON`: **139/139 green** (Phase 1 136 + HelloTransform H1/H2/H3; 2 by-design skips; 10.92s).
- Step 2 — `build-maya-2022` Release with `RBF_BUILD_MAYA_NODE=ON MAYA_VERSION=2022 MAYA_DEVKIT_ROOT=<install root>`: `rbfmax_maya.mll` linked clean (0 warnings, 0 errors). 25,088 bytes.
- Step 3 — `mayapy smoke_hellonode.py …/rbfmax_maya.mll`: **exit 0**. 4/4 contract steps passed. `compute(1.0) = 0.36787944117144233` matches `exp(-1)` bit-identically (`err=0.000e+00`).
- Step 4 — `build` Release Phase 1 regression: **136/136 green** (unchanged from v1.0.0; 10.61s).

**Two in-flight fixes discovered during executor verification** (captured here for audit; both rolled into the landed files)

1. `FindMaya.cmake` initial draft did not set `Maya_<component>_FOUND` — `find_package_handle_standard_args(HANDLE_COMPONENTS)` inspects that exact variable name, so without it the first configure failed with "missing: OpenMaya OpenMayaAnim Foundation" even though `MAYA_<Comp>_LIBRARY` had resolved. Fixed by setting `Maya_${_comp}_FOUND` alongside each library variable.
2. `plugin_main.cpp` initial draft followed the legacy Maya convention of wrapping `initializePlugin` / `uninitializePlugin` in `extern "C"`. Maya 2022's `MStatus` type lives in the inline namespace `Autodesk::Maya::OpenMaya20220000`, and MSVC strict mode rejects the combination with C2732 "linkage specification conflicts with an earlier specification". Dropped `extern "C"` in favour of plain `__declspec(dllexport)` / `__attribute__((visibility("default")))`; this is the Maya 2022+ devkit convention and the resulting C++-mangled symbols are resolved by Maya's loader through the inline-namespace ABI.

Also: `cmds.unloadPlugin()` failed with "plugin still in use" after `cmds.delete(node)` because Maya's undo stack retained a reference. Added `cmds.flushUndo()` between delete and unload in the smoke script; this is the Autodesk-documented cleanup pattern for mayapy non-interactive sessions.

A further local-env footnote: during the first mayapy invocation, a third-party Quixel MSLiveLink `userSetup.py` in the user's Python site-packages printed an unrelated traceback at import time. It did not propagate to our exit code (our smoke runs after the userSetup noise). Logged here so future runs are not mistaken for smoke failures.

**Outstanding after Slice 10A**
- **Slice 10B** — Maya 2024 validation + FindMaya patches if the 2024 devkit layout differs (expected small).
- **Slice 10C** — Maya 2025.
- **Slice 10D** — Maya 2026 + C++14 fallback `/Zc:__cplusplus` hardening for 2022 if issues surface.
- **Slice 11** — real Phase 1 kernel tap: `mRBFNode` grows dynamic array attributes (centers, targets), `fit` / `predict` forwarding, `rbfmax::solver` link. Not blocked by 10B/C/D.

---

## 2026-04-20 · Slice 09 — Benchmarks + v1.0.0 (Phase 1 finale)

**Scope**: Phase 1 closing slice. Performance baseline, public documentation (README + LICENSE), and the v1.0.0 tag. No new functional code in kernel/solver/interpolator; this slice ships measurement infrastructure, publication-ready docs, and the Phase 1 retrospective.

**Deliverables**
- `benchmarks/common.hpp` — deterministic synthetic-data helpers (`generate_uniform_cube`, `synthesize_targets`), seed `0xF5BFA8u`.
- `benchmarks/{bench_kernel, bench_solver, bench_predict}.cpp` — 11 Google Benchmark cases across 3 groups.
- `benchmarks/CMakeLists.txt` — rewritten from the Slice 05 skeleton: each `bench_*.cpp` supplies its own `BENCHMARK_MAIN()`, so we link `benchmark::benchmark` only (no `benchmark_main`). `benchmarks/.gitkeep` removed.
- `.github/workflows/ci.yml` — new `benchmark-smoke` job (Ubuntu 22.04 + GCC 11), only triggered by `workflow_dispatch` or version tag push. Daily PR CI unchanged.
- `README.md` — project-first public README (rewritten from the Slice 01 draft that stopped at v0.1.0), ~200 lines covering status, quick start, architecture diagram, feature list, build instructions, reference performance table, design decisions, roadmap, license pointer.
- `LICENSE` — Apache License 2.0 full text with "Copyright 2026 891458249" notice.
- Top-level CMake `VERSION 0.8.0 → 1.0.0`.

**Design decisions (9 locked pre-slice)**
1. **Benchmark scope: 11 cases in 3 groups** (kernel primitives / solver training / predict hot path). Each group in its own `.cpp`.
2. **CI benchmark strategy: opt-in** (`workflow_dispatch` + tag push only). Keeps PR CI fast; performance checks on demand / at release.
3. **README scale: middleweight** (~200 lines). Not a landing page, not a 30-line minimal — consistent with industrial-plugin scope.
4. **License: Apache 2.0**. Patent grant + industrial-software standard. Verified compatible with all fetched dependencies (Eigen MPL 2.0, GTest BSD 3-Clause, nlohmann/json MIT, Google Benchmark Apache 2.0).
5. **Benchmark file organisation: three `.cpp` + one `common.hpp`**. Rejected single-file variant as future Phase 2/3 benchmarks would become unwieldy.
6. **Synthetic data seed: `0xF5BFA8u`** (sequential after Slice 08's `0xF5BFA7u`).
7. **Performance target location: README reference table + DEVLOG detailed table.** README shows engineering ranges ("~0.5–1 μs"); DEVLOG lists exact measured numbers.
8. **v1.0.0 release: manual GitHub Release after tag push.** Automated release generation is a Phase 2 nice-to-have.
9. **Tech-debt: not cleared in Phase 1; explicit handoff to Phase 2** with per-item disposition in the retrospective table below.

**Measured performance** (MSVC 19.44, Windows 11, Release, 28-core Intel-class CPU @ 3.4 GHz, `--benchmark_min_time=0.01s`)

| Benchmark                               | Mean time |
|-----------------------------------------|-----------|
| `BM_Kernel_Gaussian`                    | 2.90 ns   |
| `BM_Kernel_ThinPlateSpline`             | 2.74 ns   |
| `BM_Kernel_Dispatch`                    | 2.92 ns   |
| `BM_Fit_Gaussian_N100`                  | 129 μs    |
| `BM_Fit_Gaussian_N1000`                 | 25.3 ms   |
| `BM_Fit_Gaussian_N1000_GCV`             | 445 ms    |
| `BM_Predict_Gaussian_N100`              | 2.30 μs   |
| `BM_Predict_Gaussian_N1000_Dense`       | 23.5 μs   |
| `BM_Predict_Gaussian_N1000_KdTree`      | 1.43 μs   |
| `BM_Predict_ScratchPool` (N=500)        | 11.1 μs   |
| `BM_Predict_Regular_N500`               | 11.2 μs   |

**Two honest observations from the measured data**

1. **Sub-1 μs target missed by ~43 %**. The kd-tree predict at N=1000 clocks **1.43 μs** against the blueprint's "<1 μs" goal. Not dramatic, but not met either. Likely contributors: `spatial::KdTree::knn_search`'s `std::priority_queue<pair<Scalar, Index>>` heap allocates internally on every call, and the MSVC Release build keeps `eigen_assert` checks active in the KNN inner loop. Not blocking for v1.0.0; Phase 2 may switch to a stack-array max-heap + `NDEBUG`-gated contracts.
2. **ScratchPool shows no measurable advantage at N=500** (11.1 μs vs 11.2 μs for the convenience `solver::predict`). Interpretation: MSVC Release fully inlines the per-call `ScratchPool` construction in `predict()`, so the only remaining allocation is the returned `VectorX`, which both paths share. Slice 06's zero-allocation claim holds in theory but is invisible at this size because the inner kernel evaluation (N = 500 exp calls) dominates. True validation of ScratchPool's value would require a repeated-predict loop in a tight scheduler (e.g. Maya 60 fps compute()), and an out-parameter API that eliminates the final `VectorX` return alloc. Both items queued for Phase 2.

**Local-verification surprise**
- Google Benchmark sub-build took ~49 s on first `--fresh` configure (vs the main build's <2 s) — the FetchContent populate + benchmark's own CMake configure introduces measurable latency even on a cached git clone. CI's `actions/cache` keyed on `FetchDependencies.cmake + benchmarks/CMakeLists.txt` hash will amortise this after the first run. No code change needed.

**Tech-debt register**
- None new.
- Slice 06 P6 (ScratchPool allocation-count proof) remains open — the measurement at N=500 was inconclusive. Escalated to Phase 2 with the out-parameter `predict_into(...)` API as the concrete technique.

---

## Phase 1 Retrospective

**Timeline**: 2026-04-19 (Slice 01 v0.1.0) → 2026-04-20 (v1.0.0). Compressed calendar; real calendar time dominated by design reviews and spec iteration, not code.

**Slices shipped**: 8 feature + 2 CI (02.5, 02.5.1) + 1 finale = **11 iterations**.

**Git releases** (11 tags):
v0.1.0 → v0.2.0 → v0.2.1 → v0.2.2 → v0.3.0 → v0.4.0 → v0.5.0 → v0.6.0 → v0.7.0 → v0.8.0 → **v1.0.0**.

**Code inventory (approx, at v1.0.0)**:
- `kernel/` headers + sources: ~3500 LOC
- `tests/`: ~2500 LOC (136 TEST blocks)
- `benchmarks/`: ~280 LOC (11 cases)
- `docs/` (math + schema): ~1200 LOC markdown
- `CMakeLists.txt` + `cmake/` + `.github/workflows/`: ~500 LOC infra

**What went well**
- **Pre-slice design review caught most ambiguity before code was written.** 15+ decision points per slice flagged explicitly and frozen in DEVLOG before the executor channel started work. The slice body almost never hit a "what should this do?" question mid-implementation.
- **R-09 protocol** (arithmetic self-check before dispatching numeric recommendations) matured through real failures — Slice 02's tolerance slip (`theta * 2e-7` that actually evaluates *tighter* than the original `1e-12`), Slice 07's three-way spec-vs-math contradiction (1e-8 KNN accuracy demanded in C2 while §14 proved it unreachable at ε=1). R-09 is now standard: every tolerance number gets substituted into its bound expression and mentally evaluated before being written to spec.
- **CI matrix caught latent C++11 bugs** in Slice 01 that would have silently leaked otherwise (KernelParams aggregate rules, R-11). One CI round saved one debugging session.
- **Feature branch + PR + branch protection + auto-delete** stabilised after Slice 07. From Slice 08 onward, zero friction: PR opens, CI runs, merge, auto-delete, tag. Takes ~5 min of human attention per slice.

**What to improve in Phase 2**
- **Spec pre-validation rigor.** The reviewer channel (me, in the spec voice) introduced at least 5 spec errors during Phase 1 that the executor channel (me, in the code voice) caught during implementation:
  - `EIGEN_ASSERT` vs `eigen_assert` casing (Slice 03)
  - Debug directory assumption (Slice 02)
  - `theta * 2e-7` tolerance that's tighter than the prior 1e-12 (Slice 02)
  - Three tolerance numbers off-by-orders-of-magnitude in Slice 07 (B2, C2, G1)
  - Slice 08 `std::getenv` under `/WX` (caught at build, not spec)
  Fix: the reviewer channel should run a "mental dry run" before dispatching — mentally execute each step, check every numeric claim against a written derivation, cross-reference test tolerances with the math chapters *in the same spec*.
- **R-09 extension**: now codified as "any `(tolerance, bound)` pair in the same spec must satisfy `tolerance ≥ bound × safety_margin`, substitutions checked on paper before dispatch". Written into the Phase 2 executor protocol.
- **Branch protection initial setup** had self-approve deadlock (T-08) and required GitHub UI tweaks (required review count 0 for single-contributor). Should be anticipated for any new protected-branch setup; added to Phase 2 entry conditions.
- **PR merge housekeeping**: the "click Delete branch" step was missed on Slice 05, 06, 07 consecutively. Fixed permanently by enabling "Automatically delete head branches" at repo level (T-09, verified working by Slice 08 tag closeout). Lesson: enable auto-delete on any new repo before the first PR.

**Tech-debt register carried into Phase 2**

| ID   | Description                                             | Plan                                             |
|------|---------------------------------------------------------|--------------------------------------------------|
| R-01 | CMP0169 OLD policy (Eigen 3.4 upgrade)                  | Address when Eigen 3.3 EOL                       |
| R-05 | `kernel_type_from_string` first-char switch collisions  | Rewrite when adding Wendland / Matern kernels    |
| R-10 | Eigen GitLab mirror fallback                            | No-op until observed flakiness                   |
| R-11 | MSVC permissive C++14 aggregates under `/std:c++11`     | Observational; CI catches                        |
| R-12 | Node 20 → Node 24 deprecation (2026-06)                 | Upgrade actions when v5 series ships             |
| R-13 | GCC `-Wunused-function` vs MSVC silence                 | Observational; CI catches                        |
| R-15 | Local cmake version mixing causes "Comeau" error        | Document in contributor guide                    |
| R-16 | nlohmann_json FetchContent sub-build flake (Win + VS)   | Document local workaround (SOURCE_DIR override)  |
| T-04 | `docs/math_derivation.md` §6 numbering gap              | Cosmetic; fold into future math edit             |
| T-06 | "Require branches up to date" off (single-contributor)  | Enable when second contributor joins             |
| T-07 | A6 death test not implemented (eigen_assert coverage)   | Revisit when death test harness is built         |
| —    | KdTree sub-1 μs target missed by ~43 % (Slice 09 obs.)  | Phase 2: stack-array heap + `NDEBUG`-gated asserts |
| —    | ScratchPool measurable benefit needs out-param API      | Phase 2: `predict_into(out, fr, x, pool)`        |

**Phase 2 Entry Conditions**

- Maya devkit environment setup (separate slice, not part of Phase 2's first feature slice).
- Decision on Maya version matrix (2022 / 2024 / 2025 / 2026).
- Decision on Qt6 binding (PySide6 stock vs Shiboken6 custom).
- **Required reading for any new collaborator before code changes**: this DEVLOG (every slice decision), `docs/math_derivation.md` (§1–14 numeric contract), `docs/schema_v1.md` (permanent on-disk format). Phase 1 decisions are committed, not suggestions — they are the contract that made v1.0.0 possible and will be the reference point for every v1.x / v2.x decision to come.

---

## 2026-04-20 · Slice 08 — JSON I/O with schema v1 (v0.8.0)

**Scope**: Persistence layer for trained interpolators. First real consumer of the `nlohmann/json` dependency that has been a deferred fetch function in `cmake/FetchDependencies.cmake` since Slice 02.5. Closes the last functional gap before the Slice 09 benchmark + v1.0.0 finale.

**Deliverables**
- `kernel/include/rbfmax/io_json.hpp` (~70 LOC) — `rbfmax::io_json::save / load` free functions + `kCurrentSchema` constant.
- `kernel/src/io_json.cpp` (~390 LOC) — anonymous-ns enum/scalar/matrix helpers, `iso8601_now`, `library_version_string`, `build_v1_json`, `parse_v1_json`, plus the public `save / load` with try-catch perimeters.
- `RBFInterpolator::save / load` (interpolator.hpp +9 lines, interpolator.cpp +38 lines) — convenience methods delegating to io_json with internal kdtree / pool rebuild on successful load.
- `tests/test_io_json.cpp` (~440 LOC, 14 TEST blocks across A-E categories).
- `docs/schema_v1.md` (~155 LOC) — normative schema spec, field-by-field semantics, known limitations, upgrade path.

**Design decisions (8 locked pre-slice)**
1. **Schema structure: nested** (variant B) over flat (A). Easier evolution; matches industry conventions (`package.json`, `cargo.toml`).
2. **Version strategy: integer-incrementing `"rbfmax/v<N>"`** (variant A), decoupled from package SemVer. File-format lifecycle differs from package SemVer.
3. **API: free functions + `RBFInterpolator` convenience methods** (variant B, best of both worlds — testable in isolation, ergonomic at call sites).
4. **Float precision: full 17-digit double round-trip** (variant B). nlohmann's default formatting recovers the unique IEEE double, locked by tests A1-A5 + D1 with `EXPECT_EQ` (not `EXPECT_NEAR`).
5. **Errors: bool return, noexcept, atomic update** (variant B). `out_*` is *only* mutated after the entire parse succeeds — failure leaves callers untouched. `RBFInterpolator::load` extends this by also leaving the kdtree / ScratchPool unchanged on failure.
6. **`meta.version` is diagnostic, not dispatch** (variant A). Two files differing only in `meta.created_at` deserialize identically.
7. **NaN/Inf: lossy convert to JSON null** (variant C). Pragmatic; documented in schema_v1.md and locked by test D2.
8. **Random seed: `0xF5BFA7u`** (sequential after Slice 07's `0xF5BFA6u`).

**Deviation from blueprint docx**
- The original architecture document (`docs/源文档/优化提示词：Claude 插件开发.docx`) recommended Protobuf for serialization. We chose JSON (nlohmann/json) per DEVLOG D-01 for human-readability + zero toolchain overhead. Slice 08 executes that decision. If large-rig file size becomes a bottleneck, a binary sidecar (MessagePack / CBOR) can be added in v2 without breaking JSON readability — schema_v1.md "Typical file size" + "Upgrade path" sections sketch the migration.

**Tolerance register (audit anchor)**
- A1-A5 / D1 / D2 condition_number (NaN check): exact equality (`EXPECT_EQ` on Eigen matrices, scalar `==`). The full-double-precision round-trip claim is bit-identical, not approximate.
- E1 predict-after-load consistency: `1e-14` absolute. Same arithmetic path on both sides; the margin guards against any sum-order drift Eigen might introduce.
- C2 file-readable: only asserts file size > 0; no content tolerance.

**Local-verification surprise (R-15 environment + new MSVC interaction)**
- First Release build failed with **C4996 on `std::getenv`** (TempFile helper used `std::getenv("TEMP")`). Project ships with `/WX`, so warning → error. Fix: switched the Windows branch of `TempFile` to `_dupenv_s` (heap-allocates the env value, `free()` on cleanup). Linux/POSIX path unchanged.
- First Release configure failed with `Build step for nlohmann_json failed: 1` even though the source had been git-cloned successfully. Workaround: pass `-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$(pwd)/build/_deps/nlohmann_json-src` so FetchContent skips the populate sub-build. This avoids a flaky Visual Studio sub-project step on already-cloned dependencies. CI (which always starts from a clean checkout) will not hit this; the workaround is purely a local development convenience and is not committed (no CMakeLists change needed).

**Tech-debt register**
- None new.
- `schema_v1` is now a **permanent commitment** — any future modification of `parse_v1_json` is forbidden unless it would refuse to load legacy v1 files. Future v2/v3 must add new dispatch branches.

**Workflow note**
- Fifth slice on the post-protection PR workflow. Branch `slice-08-io-json` → PR → CI (with first nlohmann/json fetch — expect cache miss + ~20s extra on the first run) → human approval → rebase merge → tag `v0.8.0`.
- Auto-delete head branches now enabled at the repo level (per Slice 07 closeout note); Slice 08 will be the first to validate that workflow change.

**Outstanding after Slice 08**
- **Slice 09**: benchmarks + **v1.0.0** — Phase 1 finale. Will populate the empty `benchmarks/` skeleton from Slice 05, validate the ScratchPool zero-allocation claim deferred from Slice 06, and establish wall-clock baselines for fit/predict at typical N/D/M combinations.

---

## 2026-04-20 · Slice 07 — RBFInterpolator facade (v0.7.0)

**Scope**: Phase 1 integration slice. Wraps the 5 lower modules (kernel / distance / kdtree / solver / scratch_pool) into a single user-facing class. End of Phase 1 API surface; Slice 08 (JSON I/O) and Slice 09 (benchmarks) are non-API work.

**Deliverables**
- `kernel/include/rbfmax/interpolator.hpp` (~130 LOC) — `InterpolatorOptions` struct and `RBFInterpolator` class (move-only, clone() for per-thread).
- `kernel/src/interpolator.cpp` (~380 LOC, linked into `rbfmax_solver` STATIC library).
- `tests/test_interpolator.cpp` (15 TEST blocks, 7 categories, random seed `0xF5BFA6u`).
- `docs/math_derivation.md §14` — KNN truncation error analysis replacing the prior SIMD-notes placeholder.

**Design decisions (8 locked pre-slice)**
1. **kdtree only for Gaussian** (decision variant C'): IMQ excluded due to long-tail non-convergence at K=32-64 (per-center contribution ~O(r⁻¹) doesn't decay fast enough).
2. **fit() fully replaces prior state**: no history retained; equivalent to fresh construct + fit.
3. **Single `InterpolatorOptions` constructor** taking kernel params only (other fields via brace-init or direct assignment).
4. **Move-only + `clone()` for per-thread**: rule-of-five hand-written for noexcept guarantees; clone() rebuilds kd-tree against the copy's centers buffer to guarantee spatial-storage independence.
5. **NOT thread-safe** on a single instance (mutable `ScratchPool` + KNN scratch buffers). Documented in header doc-block.
6. **Full getter set**: `is_fitted`, `status`, `solver_path`, `n_centers`, `dim`, `lambda_used`, `condition_number`, `uses_kdtree`.
7. **Default kdtree threshold N=256**; kernel-specific.
8. **Default K = min(N, 32) for Gaussian**; overridable via `opts_.knn_neighbors`.

**IMQ kdtree deferred** — original spec had decision 1 as variant C (Gaussian+IMQ) but arithmetic check during review showed IMQ truncation error at K=64, N=500 exceeds 1e-2. Revised to C' (Gaussian-only). Future slice could design dedicated ball-tree + adaptive K for IMQ.

**Thread safety contract** — a single `RBFInterpolator` is NOT thread-safe (the mutable `ScratchPool` and `indices_buf_` / `sq_dist_buf_` are shared per-predict-call scratch). Users needing concurrent predict call `clone()` per worker thread. Documented in both header doc-block and the class comment.

**Spec deviations (R-09 protocol, 3 items)**

These were caught during local Release verification when 3/122 tests failed against the originally-speced tolerances. All three turned out to be spec-vs-reality mismatches, not implementation bugs:

| Test | Spec tolerance | Reality | Fix |
|---|---|---|---|
| B2 `SecondFitWithDifferentDimensions` | `1e-6` sample-point residual | `~0.02` — 2D × N=50 × Gaussian ε=1 has heavily-overlapping kernels, conditioning leaves ~2% residual | Relaxed to "finite + within 0.1 of target" since B2's intent is just to verify refit-with-new-dim succeeds. |
| C2 `KnnApproxMatchesDenseForGaussian` | ε=1, 1e-8 | Per §14 (landed this slice!), at ε=1 in 3D unit cube per-center KNN truncation is ~0.37; spec contradicted its own math doc | Changed ε=8 (narrow Gaussian, near-diagonal kernel matrix) + engineering tolerance 1e-3. |
| G1 `SinCosReconstructsSameAsSolver` | `1e-6` "与 Slice 05 G1 同水准" | Slice 05 G1 (`EndToEnd.SinCosZReconstructs`) actually uses `0.1`; spec misremembered | Aligned to 0.1. |

**Lesson (carries R-09 forward)**: Spec tolerance claims must be substituted back to the math doc the spec itself cites. C2 was particularly embarrassing — the same spec that asked for 1e-8 KNN accuracy also commissioned §14 which proves 1e-8 isn't reachable at ε=1. Cross-reference before dispatch next slice.

**Tolerance register (audit anchor)**
- A2/A3 sample-point reconstruction (Gaussian/Cubic, N=20-25): `1e-6`/`1e-5` absolute — tight fits with small N, well-conditioned.
- B1 A→B refit separation check: `> 1e-3` — ensures state actually changed.
- C2 KNN vs dense: `1e-3` at ε=8, see math §14.4.
- E1 clone() identity: `1e-14` absolute — bit-identity expected since clone copies FitResult and rebuilds pool+kdtree without re-fitting.
- F2 singular: status ∈ {OK, SINGULAR_MATRIX}, no tolerance.
- G1 end-to-end RMSE: `0.1` — matches Slice 05 realistic baseline.

**Tech-debt register**
- None new by count, but one item of note:
  - `build_polynomial_row_local` duplicated in `interpolator.cpp` anonymous namespace (matches `solver.cpp`'s private helper). Accepted rather than exposing an internal helper through `solver.hpp`'s public surface. Consolidate if Slice 10+ introduces a third consumer.

**Workflow note**
- Fourth slice on the post-protection PR workflow. Branch `slice-07-interpolator` → PR → CI matrix → human approval → rebase merge → tag `v0.7.0`.
- Continued R-15 environment hygiene (two cmake versions, one per build dir).

**Outstanding after Slice 07**
- **Slice 08**: JSON I/O — serialize `FitResult` for persistence and cross-process transfer. `nlohmann_json` already fetched (deferred fetch function ready in `FetchDependencies.cmake`).
- **Slice 09**: benchmarks — first real performance validation, including ScratchPool zero-alloc claim (Slice 06 deferred). Populates the empty `benchmarks/` skeleton from Slice 05.
- **v1.0.0** at end of Slice 09 marks Phase 1 complete.

---

## 2026-04-20 · Slice 06 — ScratchPool for zero-alloc predict (v0.6.0)

**Scope**: Breathing slice after Slice 05's peak workload. Pure engineering optimization, no new math. Eliminate heap allocations in predict hot path to prepare for Maya 60fps `compute()`.

**Deliverables**
- `kernel/include/rbfmax/solver.hpp` — `ScratchPool` class (move-only, four pre-allocated `VectorX` members, `noexcept` API) + two `predict_*_with_pool` declarations, placed adjacent to the existing public API block.
- `kernel/src/solver.cpp` — `ScratchPool` ctor, `build_polynomial_row` helper (anonymous namespace), `predict_with_pool`, `predict_scalar_with_pool`, and internal refactor of `predict_batch` / `predict` / `predict_scalar` to share the pooled compute path via delegation.
- `tests/test_solver.cpp` — 9 new TEST blocks (P1/P2/P3/P4/P5/P7/P8/P9/P10); P6 zero-alloc instrumentation intentionally skipped per design decision (deferred to Slice 09 google-benchmark suite). Random seed `0xF5BFA5u` (sequential after Slice 05's `0xF5BFA4u`).

**Design decisions (3 locked pre-slice + 2 derived)**
1. **Memory strategy**: pre-allocated Eigen `VectorX` members, not `std::vector` + manual offsets and not a custom arena. Rationale: Eigen's resize-at-construction guarantees the inner predict loop hits no allocator; arena would invite ABI-mismatch headaches with Maya's TBB.
2. **API integration**: internally consume pool in `predict_batch` (transparent to users; signature unchanged) + expose `predict_with_pool` / `predict_scalar_with_pool` explicitly for Maya per-frame reuse where the node owns pool lifetime.
3. **Thread safety**: pool is NOT thread-safe; each thread / TBB task owns its own instance. Documented in header doc-block.
4. **File placement**: `ScratchPool` lives in `solver.hpp` (not a separate `scratch_pool.hpp`) — it is a solver-internal scratch type and does not warrant a new header in the public surface.
5. **Compute path unification**: `predict` / `predict_scalar` were refactored to delegate to the pool variants via an internal temporary pool. This single-sources the arithmetic and lets P4/P5 lock the bit-identity invariant (1e-14 absolute tolerance — no wiggle room, exact equality expected).

**Tolerance register (audit anchor)**
- P4 / P5 / P7 — `1e-14` absolute. `predict_with_pool` and `predict` share the *same* arithmetic sequence (delegation, not re-implementation), so equality is theoretically exact; `1e-14` is the safety margin for any reordering Eigen might insert.
- P9 — no tolerance, only `std::isfinite` (functional smoke test on a 500×500 batch).

**Tech-debt register**
- None new.
- Clears the informal "predict allocates per call" concern raised at the end of Slice 05, though `predict` / `predict_with_pool` still return `VectorX` by value (one allocation per query by interface). True O(1) per-query requires an out-parameter API; deferred to Slice 07 (`RBFInterpolator` end-to-end class).

**Workflow note**
- Third slice on the post-protection PR workflow. Branch `slice-06-scratch-pool` → PR against `main` → CI matrix → human approval → rebase merge → tag `v0.6.0`.
- Two cmake versions coexist on the dev machine (3.25 standalone for `build-dbg/`, 3.31 VS-bundled for `build/`). Build commands now route to the matching cmake binary per build dir to avoid the "No preprocessor test for Comeau" regen failure observed during Slice 06 verification. Recorded as informal R-15 (no code action needed; environment hygiene).

**Outstanding**
- Slice 07: `RBFInterpolator` end-to-end class, consumes solver + kdtree + ScratchPool. May introduce out-parameter `predict_into(out, fr, x, pool)` to close the last allocation.
- Slice 09: benchmark will validate ScratchPool actually eliminates per-iter allocations (current Slice 06 tests only verify functional equivalence).

---

## 2026-04-19 · Slice 05 — RBF Solver (v0.5.0)

**Scope**: The largest slice in Phase 1. Lands the first non-header-only module (`kernel/src/solver.cpp`) and the first STATIC library target (`rbfmax_solver`), unifying the kernel + distance + rotation + kdtree stack into the canonical RBF `fit → predict` pipeline. Slice 06+ pose-space applications, JSON I/O and Maya node integration all consume `rbfmax::solver`.

**Deliverables**
- `kernel/include/rbfmax/solver.hpp` — public API (`FitOptions`, `FitResult`, `SolverPath`, `FitStatus`, two `fit()` overloads + three `predict_*` overloads, all `noexcept`).
- `kernel/src/solver.cpp` — implementation: graded-lex monomial basis, kernel matrix builder, three-tier `LLT → LDLT → BDCSVD` fallback, QR elimination for the augmented saddle-point system, GCV via SVD closed form on a 32-point log grid `[1e-12, 1e2]`.
- `tests/test_solver.cpp` — 34 TEST blocks across 7 categories (A basics 8 / B numerics 6 / C solver-path 4 / D GCV 4 / E poly tail 4 / F batch 3 / G end-to-end 5). Random seed `0xF5BFA4u`.
- `docs/math_derivation.md §11–13` — Tikhonov regularisation, GCV closed form, QR elimination derivation. §14 placeholder reserved for Slice 06.
- `benchmarks/CMakeLists.txt` + `benchmarks/.gitkeep` — clears Slice 01 R-06 tech debt: `-DRBF_BUILD_BENCHMARKS=ON` was permanently broken because top-level CMake added `benchmarks/` as a subdirectory but no CMakeLists existed inside it.

**Design decisions (15 fixed in pre-slice review)**
1. `kLambdaMin = 1e-12` floor on regularisation parameter.
2. λ-clamp policy: silent in Release, `assert` in Debug.
3. Solver fallback ordering: `LLT → LDLT → BDCSVD`.
4. `FitResult::path` records actual solver used (audit trail).
5. Augmented system → QR elimination via Householder (not Schur complement).
6. Polynomial basis: standard monomial, graded-lex order, degree ∈ [0, 3].
7. Polynomial activation: only when `KernelType::minimum_polynomial_degree() ≥ 0`.
8. GCV grid: 32 log-spaced points over `[kLambdaMin, 1e2]`.
9. GCV evaluation: SVD closed form (no nested solves over the grid).
10. All public API `noexcept`, errors via `FitStatus` enum.
11. STATIC (not SHARED) library — folds into the eventual Maya plug-in DLL without exposing internal symbols.
12. PUBLIC link `rbfmax::kernel` so transitive consumers inherit Eigen alias.
13. `rbfmax_apply_warnings` + `rbfmax_apply_release_tuning` applied to solver TU.
14. Test infrastructure: conditional `rbfmax::solver` link inside `rbfmax_add_test()` for `test_solver` only.
15. Random seed `0xF5BFA4u` (sequential continuation from Slice 04's `0xF5BFA3u`).

**Workflow note**
- Second slice on the post-protection PR workflow (after Slice 04). Branch `slice-05-solver` → PR #2 against `main` → CI matrix → human approval → rebase merge → tag `v0.5.0`.

**Spec deviation note (R-09 protocol)**
- Test C2 (`SolverPath.DuplicatesFallbackToLDLT`) was specified to verify that near-duplicate centers force the LLT path to fail and fall back to LDLT or BDCSVD. **This premise contradicts elementary linear algebra**: for any symmetric `A`, the matrix `A + λI` with `λ ≥ kLambdaMin > 0` is *strictly* positive-definite (every eigenvalue lifted by at least λ above zero), so LLT *always* succeeds. The spec scenario can only be reproduced by either (a) `λ = 0` — forbidden by `kLambdaMin`, or (b) a non-PSD kernel — out of scope for this slice. Test relaxed to verify `status == OK` and `path != FAILED`. Documented in commit 1 message body and here.
- **Lesson**: Spec assumed numerical fragility that the project's own invariant (`kLambdaMin`) had already engineered away. Future spec reviews should cross-check pathological-input tests against the regularisation guarantees they themselves mandate.

**Tolerance register (audit anchor)**
- Reconstruction (sin·cos·z, gaussian, Runge): `1e-3` RMSE absolute.
- Train residual on noiseless data: `1e-6` absolute.
- LLT vs LDLT vs BDCSVD result agreement: `1e-9` relative.
- GCV-selected λ on noisy data: must satisfy `λ_GCV > 100·kLambdaMin` (validates auto-selection actually engaged).

**Tech-debt register additions**
- None new. R-06 (benchmarks dangling subdirectory) cleared by this slice.

**Validation**
- Local Windows MSVC 17.3 double-green: Release **98/98**, Debug **98/98** pass. By-design `GTEST_SKIP`s: `Numerics.LambdaBelowMinClampsSilently` (Release-only behaviour), `BatchPredict.DimensionalityMismatchTrapsInDebug` (Debug-only assert), `SwingTwistDecomposition.DebugAssertOnNonUnitAxis` (Slice 03 carryover, Debug-only).

**Outstanding**
- Slice 06 (kernel-tap interface / pose-space adapters) — unblocks Maya node parameter binding.
- Slice 09 (benchmarks) — `bench_solver.cpp`, `bench_kdtree.cpp` to populate the now-functional benchmarks/ skeleton.

---

## 2026-04-19 · Slice 04 — kd-tree spatial index (v0.4.0)

**Scope**: Introduce nearest-neighbor acceleration for RBF interpolation under large-sample regimes. Required by Slice 05 (solver) when N > ~1000 and per-query O(N) cost becomes prohibitive for 60fps playback.

**Deliverables**
- `kernel/include/rbfmax/kdtree.hpp` — header-only, ~230 LOC, array-backed, variance-based split.
- `tests/test_kdtree.cpp` — 11 TEST blocks, brute-force parity validation on 500-sample × 100-query scan.
- `docs/math_derivation.md §10` — complexity & pruning geometry, edge-case behavior table.

**Design decisions (from pre-slice design review)**
- Array-backed nodes (vs linked) for cache locality.
- Caller-managed sample lifetime (zero-copy via `Eigen::Ref<const MatrixX>`).
- Out-parameter buffers (zero-alloc hot path).
- Recursive (vs iterative-with-stack) — depth log₂N bounded.
- `std::priority_queue` as max-heap on squared distance.
- Variance-based split dimension.
- Median split value (`std::nth_element`).
- `k>N` silently clamps, returns actual count.
- Output ascending by distance, squared distances (no `sqrt`).

**Workflow note (first post-protection slice)**
- Slice 04 is the first slice developed on a feature branch (`slice-04-kdtree`) merged via PR, after Branch Protection on `main` was enabled following Slice 03's second consecutive green CI. Direct push to main is now rejected.
- Tag `v0.4.0` will be applied post-merge on the main HEAD by a separate authorization round.

**Spec deviation note**
- Test K3 was specified as a "tree depth bounded" probe, but the public KdTree API deliberately does not expose `nodes_` (encapsulation invariant). Replaced K3 with a stronger functional property: build correctness on N=1000 verified via 1-NN parity against brute force. The `O(log N)` query touch count is implicitly exercised by K11's <500ms budget on N=10000. Documented in commit 1 message body.

**Tech-debt register additions**
- None new. A6-style death tests deliberately avoided (see T-07).

**Outstanding**
- Slice 05 (solver) — next in dependency graph; first slice to unify kernel + distance + rotation + kdtree into an end-to-end RBF interpolation pipeline with Tikhonov regularization.

---

## 2026-04-19 · Slice 03 — Quaternion algebra (v0.3.0)

**Scope**: First pose-space math primitive layer. Delivers the `rbfmax::rotation` submodule required by Slice 05 (solver) when RBF centers are unit quaternions rather than Euclidean anchors.

**Deliverables**
- `kernel/include/rbfmax/quaternion.hpp` — three APIs in `rbfmax::rotation::`:
  - `decompose_swing_twist(q, axis)` returning `SwingTwist{swing, twist}`
  - `log_map(q) -> Vector3` (rotation vector, short-path)
  - `exp_map(v) -> Quaternion` (rotation vector → unit quaternion)
- `tests/test_quaternion.cpp` — 16 TEST blocks (15 active + 1 GTEST_SKIP), ~5000+ assertions via 1000-sample fixed-seed batches.
- `docs/math_derivation.md §7` (Swing-Twist algebra) and `§8` (Log/Exp Lie algebra) — full derivations. The previous `§6` placeholder was superseded; remaining solver placeholder moved to `§9`.

**Design decisions (from pre-slice design review)**
- Axis parameterization: `Vector3` (not `Axis3` enum) — supports local bone axes that are not world axes.
- Unit-axis contract: caller-enforced + `eigen_assert` in Debug (mirrors `distance.hpp` geodesic contract).
- 90° swing degeneracy: return `twist = Identity` (twist unobservable).
- Log map double cover: internally flip `q → -q` when `w<0` (shortest-path semantics, standard for interpolation pipelines).
- Taylor threshold: `|v| = 1e-8` — conservative, Taylor truncation error `O(1e-32) ≪ ε_mach`.
- Random seed: `0xF5BFA2u` (Slice 02 used `0xF5BFA1u`; sequential seeds per slice for determinism without cross-test collisions).

**Test tolerance register (audit anchor for future review)**
- Identity/zero round-trips: `1e-14` absolute
- `Log∘Exp`, `Exp∘Log` round-trip: `1e-10` absolute
- Taylor near-zero: `1e-14` absolute
- π-boundary: `1e-8` absolute (asin saturation)
- Unit-norm preservation: `1e-14` absolute

**Spec deviation note**
- Spec specified `EIGEN_ASSERT` (uppercase); Eigen 3.3.9 only exposes `eigen_assert` (lowercase) as the public contract macro. Used lowercase form throughout `quaternion.hpp` and its doc comments. Documented in commit 1 message body and here as a single-source-of-truth audit trail.

**Tech-debt register additions**
- None new. Slice 03 clean, no workarounds.

**Outstanding after Slice 03**
- Branch protection still pending (needs 2 consecutive green main runs; v0.2.2 was 1st green if remote CI passed, Slice 03 push will be 2nd if green).
- Slice 04 (kdtree) next in dependency graph.

---

## 2026-04-19 · Slice 02.5.1 — First CI-caught regression (v0.2.2)

**Context**: Slice 02.5 introduced CI; its very first run on main (trigger: push of `5330836`) failed the `ubuntu-gcc-release` job while both Windows MSVC Release and Debug jobs passed. This is the first CI-caught latent regression and exactly the reason CI was prioritized before Slice 03.

**Root cause**

`KernelParams` (introduced in Slice 01) used default member initializers to encode defaults:

```cpp
struct KernelParams {
    KernelType type {KernelType::kGaussian};
    Scalar     eps  {1.0};
};
```

Under C++11 `[dcl.init.aggr]/1`, any brace-or-equal-initializer on non-static data members disqualifies the class from being an aggregate. C++14 lifted this restriction. MSVC permissively accepts C++14 aggregates under `/std:c++11`, which masked the bug locally. GCC 11 with `-std=c++11 -Wpedantic` correctly rejected the call site `KernelParams{kGaussian, 2.0}` in `tests/test_kernel_functions.cpp:229`.

**Fix**

Replaced default member initializers with two explicit constructors:

- `KernelParams() noexcept` — defaults to `{kGaussian, 1.0}`
- `KernelParams(KernelType, Scalar) noexcept` — explicit 2-arg

Semantic equivalence at all call sites preserved; no test changes.

**Tech-debt register additions**

- R-11 (new): MSVC silently accepts C++14 aggregate rules under `/std:c++11`. Any use of C++14-only language features must be audited manually or exposed by CI. A future `fix(cmake)` could add `/Zc:__cplusplus` already present plus a comment warning, but MSVC has no equivalent of `-Wpedantic` for aggregate rules.

**Lesson**

- CI-before-new-features was the right call. Slice 02.5 bought 24h of calendar time, caught a C++11 bug that would have silently leaked into every subsequent slice, and paid for itself on day 1.

---

## 2026-04-19 · Slice 02.5 — CI baseline (v0.2.1)

**Scope**: Bootstrap GitHub Actions workflow covering the three committed compiler/build permutations.

**Deliverables**
- `.github/workflows/ci.yml` — three-job matrix:
  - `windows-msvc-release` on windows-2022, MSVC 17.x, Release
  - `windows-msvc-debug`   on windows-2022, MSVC 17.x, Debug
  - `ubuntu-gcc-release`   on ubuntu-22.04, GCC 11, Release
- FetchContent dependency cache keyed on `cmake/FetchDependencies.cmake` hash.
- Test log artifacts uploaded on failure for post-mortem.
- Concurrency group cancels superseded runs on the same ref.

**Out of scope (deferred)**
- macOS runner (low Maya usage on that platform)
- GCC 4.8.2 (Maya 2018; needs CentOS 6 container — defer to Phase 2)
- Clang, sanitizers, static analysis — revisit at Slice 05 or later
- Branch protection rule on main — to be enabled after the workflow has two consecutive green runs on main.

**Version note (historical)**
- The `v0.1.0` tag points at the `feat(kernel)` bootstrap commit (`a8f0143`), because Slice 01 did not separate a `chore(release)` commit. From Slice 02 onward releases are isolated to their own `chore(release)` commits for cleaner audit.

**Tech-debt register**
- T-03 (new): branch protection rule not yet enabled. Enable after two consecutive green main runs.
- R-10 (new): if CI matrix flakes from GitHub runner network instability hitting GitLab-hosted Eigen, consider bundling Eigen as a git submodule or upstreaming to an internal mirror.

---

## 2026-04-19 · Slice 02 — Distance Metrics

**阶段**：Phase 1 (Mathematical Kernel)
**切片**：02/09
**版本跃迁**：`0.1.0` → `0.2.0`
**Commits**：
- [`2b59a8b`](https://github.com/891458249/RBF_MAX/commit/2b59a8b) · `fix(kernel)` TPS 负 r 契约对齐
- [`09d909a`](https://github.com/891458249/RBF_MAX/commit/09d909a) · `feat(distance)` 距离度量主体
- _本条目所在的 `chore(release)` 待回填_

**会话来源**：Claude Opus 4 架构师协作（含切片 ① 评审 + 切片 ② 评审两轮）

### 交付物

| 路径 | 行数 | 说明 |
|---|---|---|
| `kernel/include/rbfmax/distance.hpp` | 117 | 欧氏 + 四元数测地距离，命名空间 `rbfmax::metric` |
| `tests/test_distance.cpp` | 254 | 14 个 TEST，含 1000 组三角不等式回归 |
| `docs/math_derivation.md` §5 | +115 行 | 反号归一证明、acos/asin 分支判据、Lipschitz 误差上界 |
| `kernel/include/rbfmax/kernel_functions.hpp` | 注释修订 | TPS 负 r 契约从"NaN"改为"clamp 到 0"，与实现一致 |
| `tests/test_kernel_functions.cpp` | +3 TEST | `KernelContract.*` 锁定负 r 三档行为（奇/偶/clamp） |
| `tests/CMakeLists.txt` | +1 行 | 注册 `test_distance` |
| `CMakeLists.txt` | VERSION 0.1.0 → 0.2.0 | |
| `CHANGELOG.md` / `DEVLOG.md` | — | 双层日志追加 |

### 关键技术决策

1. **距离接口使用 Eigen 模板签名**
   - 切片 ② 评审建议 1。避免 `const VectorX&` 强制调用方构造动态向量导致的堆分配。
   - 实现以 `template <typename DerivedA, typename DerivedB>` 接收 `Eigen::MatrixBase<>`，编译期 size 不匹配由 Eigen 静态断言捕获。

2. **四元数测地距离双分支**
   - 常规区间用 `2·acos(clamp(|dot|, -1, 1))`；近单位 `|dot| ≥ 1 - kQuatIdentityEps (1e-14)` 切换到 `2·asin(sqrt(1 - dot²))`。
   - 数学推导 §5.2.3 证明两支的相对误差上界分别为 `√ε_mach ≈ 1.5e-8` 与 `~1e-7`；后者是双精度物理精度下限。

3. **单位化契约：Release 信任、Debug 校验**
   - 切片 ② 评审建议 2。热路径零分支；`assert(|‖q‖² − 1| < 1e-6)` 仅 Debug 生效。
   - 零四元数作为未定义行为写入头部注释，过滤责任上移至 Maya 节点的 attribute ingress。

4. **三角不等式抽样 1000 次、种子固定**
   - 切片 ② 评审建议 3。原提案 10 次远不够统计学严谨；1000 次耗时 <10ms 但能稳定捕获任何数值实现 bug。
   - 种子 `0xF5BFA1u` 硬编码，CI 断言永不 flake。

5. **TPS 契约修复类型归为 `fix:` 而非 `refactor:`**
   - 切片 ② 评审建议 4。Conventional Commits 按"修复的问题"分类，不按"改的文件"分类；文档-实现的 drift 是缺陷，SemVer PATCH 归零随 MINOR 抬升自然处理。

### 风险 & TODO（新增；延续自切片 01 的保留）

| ID | 状态 | 风险 |
|---|---|---|
| R-01 | 保留 | `CMP0169 OLD` 依赖，与 Eigen 3.4 升级联动 |
| R-02 | 保留 | CentOS 6 + GCC 4.8.2 实机未验证 |
| R-03 | 保留 | `static constexpr` 命名空间常量潜在警告 |
| R-04 | 保留 | Eigen 3.3.9 `-Wdeprecated-declarations` 在 GCC 11 |
| R-05 | 保留 | `kernel_type_from_string` 首字符 switch（Wendland/Matern 引入时重构） |
| R-06 | 新增 | `CMakeLists.txt` 引用不存在的 `benchmarks/` 子目录，启用 `RBF_BUILD_BENCHMARKS=ON` 会失败；切片 ⑨ 前加 `if(EXISTS)` 守卫 |
| R-07 | 新增 | `RowMatrixX` 与 `rbfmax_apply_release_tuning` 均为"暂时死代码"，等 `io_json.hpp` / solver static lib 接入时消费 |
| R-08 | 新增 | 四元数入参契约要求单位化，但 Phase 2 Maya 节点若直接连接 `rotate` 属性的欧拉→四元数转换链可能引入 `1e-7` 级漂移；需要在节点 `compute()` 入口做强制 `normalized()` |
| T-01 | 保留 | commit-msg hook 未接入 |
| T-02 | 保留 | GitHub Actions CI 矩阵未接入 |
| T-03 | 新增 | 性能 benchmark 未覆盖 `quaternion_geodesic_distance`（在 1M 调用/帧场景下可能是瓶颈），Slice ⑨ 补齐 |

### 切片内验收门

| 检查项 | 状态 |
|---|---|
| `test_distance` 新增 14 个 TEST / ≥ 40 断言 | ✅ 落盘 |
| 三个四元数病态点（`q=q'`、`q=-q'`、`dot≈0`）全覆盖 | ✅ 落盘 |
| 三角不等式 1000 组抽样固定种子 | ✅ 落盘 |
| asin 回退分支误差上界文档化 | ✅ `math_derivation.md §5.2.3` |
| 零四元数契约明确 | ✅ `distance.hpp` 头 + `math_derivation.md §5.2.4` |
| 本地编译 + ctest 验证 | ⏳ 待用户本地执行 |

### 下一切片依赖

Slice 03 — `quaternion.hpp`（Swing-Twist 分解、Log/Exp map）将消费本切片的 `kQuatIdentityEps` 常量与 `quaternion_abs_dot` 工具。

### Hotfixes (during Slice 02 admission gate)

Three-round failure → green sequence during the local double-build verification, documented here for historical accuracy and to seed the tech-debt register.

**Round 1 — MSVC C4819 under code page 936**
Slice 01 was never compiled under MSVC; UTF-8 sources without BOM raised C4819 → C2220 under `/WX`. Fix: `cmake/CompilerFlags.cmake` gained `/utf-8` in the MSVC branch.
→ committed as `build(build): add /utf-8 ...`

**Round 2 — Arithmetic slip in the tolerance recommendation (R-09)**
The review channel recommended `EXPECT_NEAR(d, theta, theta * 2e-7)`, which at θ=1e-7 evaluates to `2e-14` — 50× tighter than the original `1e-12`, not looser as intended. Root cause: failing to mentally substitute θ before dispatching a numeric tolerance. Remedy internalized: any future tolerance recommendation must be preceded by explicit "dimension / substituted value / margin" self-check.

**Round 3 — §5.2.3 doc phrasing misled the test author**
"1×10⁻⁷ 相对" in §5.2.3 was literally unbounded as θ → 0; the test author (this channel's past self) consumed it as if it were a well-defined relative error. Fix: prune the ambiguous "相对" clause and add an explicit caveat that tests must use absolute tolerances.
→ committed as `fix(tests): align near-identity ...`

**Tech-debt register additions**
- R-09 (new): arithmetic slip protocol — numeric recommendations must include substituted-value self-check before dispatch.

**Outstanding after Slice 02 admission**
- `v0.1.0` / `v0.2.0` tags not yet pushed (pending next authorization).
- Slice 02.5 (CI matrix) not yet started.

---

## 2026-04-18 · Slice 01 — Kernel Math Functions

**阶段**：Phase 1 (Mathematical Kernel)
**切片**：01/09（阶段一共 9 个切片计划）
**版本跃迁**：— → `0.1.0`
**Commit**：[`a8f0143`](https://github.com/891458249/RBF_MAX/commit/a8f0143)
**会话来源**：Claude Opus 4 架构师协作

### 交付物

| 路径 | 行数 | 说明 |
|---|---|---|
| `CMakeLists.txt` | 78 | 顶层项目；INTERFACE 目标 `rbfmax::kernel` |
| `cmake/CompilerFlags.cmake` | 75 | MSVC/GCC 警告矩阵；LTO/SIMD 分档；fast-math gating |
| `cmake/FetchDependencies.cmake` | 105 | Eigen 3.3.9 手动 populate；GTest 1.12.1；json/benchmark 延迟拉取 |
| `kernel/include/rbfmax/types.hpp` | 108 | `Scalar=double`、Eigen 别名、数值常量 |
| `kernel/include/rbfmax/kernel_functions.hpp` | 233 | 6 核函数 + 导数 + 调度器 + 字符串往返 |
| `tests/CMakeLists.txt` | 45 | `gtest_discover_tests` 注册助手 |
| `tests/test_kernel_functions.cpp` | 229 | 7 个 TEST 套件，34 个断言 |
| `docs/math_derivation.md` | 140 | LaTeX 推导、L'Hôpital 证明、有限差分容差分析 |
| `.gitignore` / `.gitattributes` / `.clang-format` | — | 工程基建 |
| `CHANGELOG.md` / `DEVLOG.md` / `COMMIT_CONVENTION.md` | — | 文档治理 |

### 关键技术决策

1. **C++11 强制基线**
   - 原文档建议 C++17，但编译矩阵包含 **GCC 4.8.2 (Maya 2018 Linux)**，强制退回 C++11。
   - 放弃 `std::optional` / 结构化绑定 / `if constexpr` / 内联变量。
   - 命名空间常量以 `static constexpr` 声明，规避 C++11/14 的非 inline ODR 陷阱。

2. **`Scalar = double` 单一内部精度**
   - 仅在 I/O 边界（Maya `MDataBlock`、JSON、UE5 导出）下转 `float`。
   - 规避 acos-based 四元数测地距离中的灾难性抵消。

3. **命名空间 `rbfmax::`（带前缀）**
   - 避开 `rbf::` 的全局符号污染风险（工作室常与物理/ML 库同构）。

4. **Thin Plate Spline 的 NaN 防御**
   - `r ≤ kLogEps = 1e-30` 直接返回 `0`；NaN 输入仍透明穿透（`r != r` 短路）。
   - L'Hôpital 极限证明见 `docs/math_derivation.md §2.4`。

5. **Inverse Multiquadric 导数数值稳定改写**
   - 原式 `-ε² r · (1 + (εr)²)^(-3/2)` 等价写作 `-ε² r / (den · √den)`，只做一次 `sqrt + div`；长尾区域精度提升 ~2 数量级。

6. **Eigen 3.3.9 手动 populate**
   - 避开 Eigen 自带 CMake 配置中的 `uninstall`/`docs`/`blas` 伪目标污染。
   - 用 `INTERFACE` 别名 `Eigen3::Eigen` + `SYSTEM` include 方向，屏蔽 `-Wshadow` 等警告外泄。
   - 注入 `EIGEN_MPL2_ONLY` 禁用 LGPL 子特性（合规前置）。

7. **fast-math 默认关闭**
   - 破坏 NaN/Inf 传播语义。
   - 作为 `RBF_ENABLE_FAST_MATH` 显式选项保留；开启时提示风险。

8. **依赖镜像机制**
   - `RBFMAX_DEPS_MIRROR` 变量可整体替换 Git 源，适配气隙构建农场。

### 风险 & TODO

| ID | 风险 | 缓解 / 计划 |
|---|---|---|
| R-01 | **FetchContent_Populate 单参形式在 CMake 3.28+ 被弃用，3.30 转错误** | 当前已 `cmake_policy(SET CMP0169 OLD)` 暂抑；阶段一收尾前迁移至 3.28 新式用法 |
| R-02 | **CentOS 6 + GCC 4.8.2 实机未验证** | 待用户提供 Maya 2018 devkit 或 CI 容器后跑一轮；否则兼容性宣称仍是声明级 |
| R-03 | **`static constexpr` 命名空间常量在某些 GCC 版本触发 `-Wunused-const-variable`** | 当前警告集未启用；待 CI 启用时视情况 `[[gnu::unused]]` 标注 |
| R-04 | **Eigen 3.3.9 在 GCC 11 `-Wdeprecated-declarations` 触发 `register` 关键字警告** | 已通过 `SYSTEM` include 屏蔽；若未来升级 Eigen 3.4 再复核 |
| R-05 | **`kernel_type_from_string` 的手写 switch-strcmp 维护性差** | 核函数数量到 10+ 时重构为 `std::unordered_map<std::string, KernelType>`（构造期一次初始化）|
| T-01 | 未接入 commitlint / pre-commit hook | 阶段一尾声接入 Husky-CMake 或纯 bash hook |
| T-02 | 未接入 GitHub Actions CI 矩阵 | 切片 ⑨ bench 交付时同步接入 |

### 切片内验收门

| 检查项 | 状态 |
|---|---|
| 本地 MSVC 2022 编译通过 | ⏳ 待用户本地验证 |
| 34 个单测全绿 | ⏳ 待验证 |
| `/W4 /WX` 零警告 | ⏳ 待验证 |
| 跨平台宣称（Linux GCC 11） | ⏳ 待 Linux 环境验证 |

### 下一切片依赖

Slice 02 — `distance.hpp` 需要依赖本切片的 `types.hpp`，不依赖 `kernel_functions.hpp`。
Slice 03 — `quaternion.hpp` 将与 Slice 02 并行设计（共享 `kQuatIdentityEps` 常量）。

---

## 模板（每条新条目使用）

```markdown
## YYYY-MM-DD · Slice NN — <标题>

**阶段**：Phase N
**切片**：NN/Total
**版本跃迁**：X.Y.Z → X.Y.(Z+1)
**Commit**：<git short sha>
**会话来源**：<对话环境/参与者>

### 交付物
| 路径 | 行数 | 说明 |

### 关键技术决策
1. ...

### 风险 & TODO
| ID | 风险 | 缓解 / 计划 |

### 切片内验收门
| 检查项 | 状态 |

### 下一切片依赖
```
