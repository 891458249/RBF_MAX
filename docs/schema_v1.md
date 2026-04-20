# RBF_MAX JSON Schema — v1

**Current schema tag**: `rbfmax/v1`

First shipped in: Slice 08 (v0.8.0).

## Purpose

Defines the on-disk JSON format for a trained RBF interpolator
(`RBFInterpolator::save` / `RBFInterpolator::load`, or the underlying
`rbfmax::io_json::save` / `load` free functions). Intended for
persistence across process restarts and for cross-host transfer of
trained interpolators.

The schema deliberately favours **human-readability and forward
auditability** over wire-size efficiency. Binary alternatives
(MessagePack, CBOR) may be added as optional sidecars in a future
schema version if file size becomes a bottleneck.

## Structure

```json
{
  "schema": "rbfmax/v1",
  "meta": {
    "library": "rbfmax",
    "version": "0.8.0",
    "created_at": "2026-04-20T12:34:56Z"
  },
  "config": {
    "kernel": {
      "type": "Gaussian",
      "eps": 1.0
    },
    "poly_degree": -1,
    "kdtree_threshold": 256,
    "knn_neighbors": 0,
    "force_dense": false
  },
  "training": {
    "lambda_used": 1e-08,
    "solver_path": "LLT",
    "status": "OK",
    "condition_number": null,
    "residual_norm": 0.001234
  },
  "data": {
    "centers":     {"rows": 20, "cols": 3, "values": [[...], ...]},
    "weights":     {"rows": 20, "cols": 1, "values": [[...], ...]},
    "poly_coeffs": {"rows": 0,  "cols": 1, "values": []}
  }
}
```

## Field semantics

### Top-level

| Path | Type | Required | Notes |
|---|---|---|---|
| `schema` | string | yes | Exact match `"rbfmax/v1"`; load fails for any other value. |
| `meta` | object | no | Diagnostic only; missing or partial `meta` does not fail load. |
| `config` | object | yes | Maps 1:1 to `rbfmax::InterpolatorOptions`. |
| `training` | object | yes | Maps 1:1 to selected `solver::FitResult` fields. |
| `data` | object | yes | Trained matrices. |

### `meta.*` (diagnostic, optional)

| Path | Type | Notes |
|---|---|---|
| `meta.library` | string | Always `"rbfmax"`. |
| `meta.version` | string | Library SemVer at write time, e.g. `"0.8.0"`. |
| `meta.created_at` | string | ISO 8601 UTC timestamp at write time. |

### `config.*` (required)

| Path | Type | Notes |
|---|---|---|
| `config.kernel.type` | string | One of `Linear`, `Cubic`, `Quintic`, `ThinPlateSpline`, `Gaussian`, `InverseMultiquadric`. Case-sensitive. |
| `config.kernel.eps` | number\|null | Shape parameter. `null` round-trips as NaN (lossy). |
| `config.poly_degree` | integer | -1 disables polynomial tail; 0–3 enables degree N. |
| `config.kdtree_threshold` | integer | n_centers ≥ this engages kd-tree (Gaussian only). |
| `config.knn_neighbors` | integer | 0 = auto (`min(N, 32)`); positive = fixed K. |
| `config.force_dense` | boolean | Debug override; if true, never engage kd-tree. |

### `training.*` (required)

| Path | Type | Notes |
|---|---|---|
| `training.lambda_used` | number\|null | Final regularisation parameter actually used. |
| `training.solver_path` | string | One of `LLT`, `LDLT`, `BDCSVD`, `FAILED`. |
| `training.status` | string | One of `OK`, `INSUFFICIENT_SAMPLES`, `SINGULAR_MATRIX`, `INVALID_INPUT`. |
| `training.condition_number` | number\|null | -1 / null when the solver path did not compute one. |
| `training.residual_norm` | number\|null | `||Aw + Pv − y||_F / ||y||_F`. |

### `data.*` (required)

Each of `centers`, `weights`, `poly_coeffs` is an object with this shape:

```json
{ "rows": <integer>, "cols": <integer>, "values": [ [..row 0..], [..row 1..], ... ] }
```

- `rows` and `cols` must be non-negative integers.
- `values` is an array of length `rows`; each inner array has length `cols`.
- Element type is number\|null (per the NaN/Inf rule below).
- Mismatched outer/inner sizes cause load failure.
- An empty matrix is encoded as `{"rows": 0, "cols": <C>, "values": []}` —
  valid (e.g. `poly_coeffs` for kernels with no polynomial tail).

## Known limitations

- **NaN, +Inf, -Inf** are serialized as JSON `null` (JSON spec lacks IEEE
  special values) and deserialized back as **NaN**. This means
  `+Inf → null → NaN` is a *lossy* conversion — the sign and infiniteness
  are not recovered. If your `FitResult` legitimately contains Inf,
  expect data loss across a save/load round-trip. Documented contract:
  `IoJsonFidelity.NanAndInfAreLossyConvertedToNaN`.
- `meta.*` fields are diagnostic; they are *not* consulted during the
  load dispatch. Two files that differ only in `meta.created_at` deserialize
  to identical `(opts, FitResult)` pairs.
- **No forward compatibility**: a v1 loader cannot read future v2 files
  (it returns false on unknown `schema`). v1 files remain readable by any
  future version through the permanent `parse_v1_json` dispatch branch.

## Upgrade path (future v2, v3, ...)

When breaking changes are needed:

1. Bump schema tag to `rbfmax/v2` and add `build_v2_json` + `parse_v2_json`
   in `kernel/src/io_json.cpp`.
2. Keep `parse_v1_json` untouched and add its dispatch branch in `load`.
3. `save` defaults to writing the latest version.
4. Document the breaking changes in this file under a new "## v2 changes"
   section (do not delete v1 documentation).

**Hard rule**: never delete or modify prior `parse_vN_json` functions —
they are historical artifacts required for loading legacy files. A test
suite per schema version is the audit anchor.

## Typical file size

| Rig profile | Approx size |
|---|---|
| Small (N=50, D=3, M=1, no poly) | ~3 KB |
| Medium (N=500, D=3, M=5, poly deg 1) | ~40 KB |
| Large (N=5000, D=10, M=10, poly deg 2) | ~2 MB |

JSON is verbose by design. If file size becomes a bottleneck on rigs
with very large `N`, a future v2 may introduce an optional binary
(MessagePack or CBOR) sidecar referenced from the JSON envelope, so
that small/medium rigs continue to enjoy plain-text auditability while
large rigs can opt into compact serialization.
