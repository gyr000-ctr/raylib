# glTF 2.0 morph target (blend shape) tests

Reproducible, fully headless tests for raylib's glTF morph target loading and
animation. No display server, GPU or network is required.

## Layout

- `gen/gen_morph_assets.py` — self-contained generator (Python 3 standard
  library only) that emits `.glb` assets and matching `*.ref.json` reference
  files. References are computed directly from the glTF 2.0 specification
  equations (`morphed = base + Σ weight·delta`, LINEAR/STEP/CUBICSPLINE).
- `assets/` — generated binary assets and reference files (regenerable).
- `test_morph.c` — functional/per-vertex tests on the software renderer.
- `test_morph_gpu.c` — GL 3.3 vertex-buffer (VBO) tests via headless EGL.
- `test_regression.c` — guard that every pre-existing example glTF/GLB still
  loads without any morph data leaking in.
- `mini_json.h`, `egl_headless.h` — tiny test-only helpers.

## Coverage

| Asset | What it exercises |
|-------|-------------------|
| `morph_single_linear.glb` | single target, LINEAR, default weights, names, position/normal/tangent deltas, animation switch, reset, manual weights |
| `morph_multi_defaults.glb` | multiple targets blended together, default weights, texcoord deltas |
| `morph_step.glb` | STEP interpolation |
| `morph_cubicspline.glb` | CUBICSPLINE interpolation, two targets, no per-frame allocations |
| `morph_skin_coexist.glb` | skin and morph on the same model (separate animations) |
| `morph_combined.glb` | one animation carrying both bone and weights channels (internal morph-then-skin order) |
| `morph_multi_primitive.glb` | one weights channel driving all primitives of a node |
| `morph_transformed_node.glb` | node TRS transform on base + morph deltas (normal inverse-transpose, translation excluded) |
| `morph_over_limit.glb` | more than `MAX_MESH_MORPH_TARGETS` → graceful truncation + warning |
| `morph_non_finite.glb` | binary NaN weight → clamped to 0 + warning |
| `morph_sparse.glb` | sparse accessor target → attribute skipped, model still valid |
| `morph_missing_accessor.glb` | dangling accessor index → explicit load failure |
| `plain_no_morph.glb` | plain model, no morph allocation/regression |

The per-vertex tolerance is `1e-4` model units, matching the project requirement.

## Running

```sh
# Functional + legacy regression (in-repo rlsw software renderer, headless)
./run_tests.sh

# GL 3.3 GPU vertex-buffer path (headless EGL + llvmpipe)
./run_gpu_tests.sh
```

Regenerate the assets at any time:

```sh
python3 -I gen/gen_morph_assets.py
```

`run_tests.sh` also regenerates automatically when `REGEN_ASSETS=1`.
Both scripts build raylib out-of-tree under `../../artifacts/` so the source
tree stays clean. An AddressSanitizer/UBSan build is provided under
`../../artifacts/build-asan/`.

## Expected result

- `Checks: N | Failures: 0` for the functional suite
- `Regression checks: N | Failures: 0` for the legacy suite
- `GPU Checks: N | Failures: 0` for the GPU suite
