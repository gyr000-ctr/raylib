#!/usr/bin/env bash
# Build and run the morph target tests headlessly against the in-repo
# software renderer (PLATFORM_MEMORY + GRAPHICS_API_OPENGL_SOFTWARE).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/bin"
mkdir -p "$OUT"

# 1. Build raylib (out-of-tree, CPU pipeline, no audio, no GL/window deps)
bash "$HERE/../../artifacts/build-memory/build.sh"

# 2. (Re)generate the test assets if requested or missing
if [ "${REGEN_ASSETS:-0}" = "1" ] || [ ! -f "$HERE/assets/morph_single_linear.glb" ]; then
    python3 -I "$HERE/gen/gen_morph_assets.py"
fi

# 3. Compile the test harness
gcc -std=c99 -D_GNU_SOURCE -DPLATFORM_MEMORY -DGRAPHICS_API_OPENGL_SOFTWARE \
    -DSUPPORT_GPU_SKINNING=0 \
    -Wall -Wextra -Wno-unused-parameter -g -O1 \
    -I"$ROOT/src" \
    "$HERE/test_morph.c" \
    -L"$ROOT/artifacts/build-memory/out" -lraylib_sw \
    -lm -lpthread -ldl -lrt \
    -o "$OUT/test_morph"

# 3b. Compile the legacy-asset regression guard
gcc -std=c99 -D_GNU_SOURCE -DPLATFORM_MEMORY -DGRAPHICS_API_OPENGL_SOFTWARE \
    -DSUPPORT_GPU_SKINNING=0 \
    -Wall -Wextra -Wno-unused-parameter -g -O1 \
    -I"$ROOT/src" \
    "$HERE/test_regression.c" \
    -L"$ROOT/artifacts/build-memory/out" -lraylib_sw \
    -lm -lpthread -ldl -lrt \
    -o "$OUT/test_regression"

# 4. Run from the test directory so relative asset paths resolve
cd "$HERE"
MORPH_ASSET_DIR="$HERE/assets" "$OUT/test_morph"

# 5. Regression over all pre-existing example glTF/GLB assets
RAYLIB_REPO_ROOT="$ROOT" "$OUT/test_regression"
