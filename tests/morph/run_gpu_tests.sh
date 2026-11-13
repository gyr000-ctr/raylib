#!/usr/bin/env bash
# Build and run the GPU-backend (OpenGL 3.3 core) morph tests headlessly.
# Uses EGL + Mesa llvmpipe (swrast), so no display server or /dev/dri access
# is required beyond the software GL libraries.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/bin"
mkdir -p "$OUT"

# 1. Build the GL 3.3 module subset with the default GPU skinning enabled
bash "$ROOT/artifacts/build-gpu/build.sh"

# 2. Compile the headless EGL test driver
gcc -std=c99 -D_GNU_SOURCE -DGRAPHICS_API_OPENGL_33 \
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -g -O1 \
    -I"$ROOT/src" -I"$HERE" \
    "$HERE/test_morph_gpu.c" \
    -L"$ROOT/artifacts/build-gpu/out" -lraylib_gl33 \
    -lEGL -lGL -lm -lpthread -ldl -lrt \
    -o "$OUT/test_morph_gpu"

# 3. Run with the software renderer forced, independent of physical GPU access
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
MORPH_ASSET_DIR="$HERE/assets" "$OUT/test_morph_gpu"
