#!/usr/bin/env bash
# Regenerates roblox/tests/golden/sway_noise.luau from tools/golden/sway_noise.c, which
# mirrors the SPT-SWAY (MIT) noise hash with C#'s 32-bit arithmetic.
# Needs a C compiler. Run from anywhere.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
build="${BUILD_DIR:-$repo/roblox/tools/golden/build}"
mkdir -p "$build"
cc -O2 "$here/sway_noise.c" -lm -o "$build/sway_noise"
"$build/sway_noise" "$repo/roblox/tests/golden/sway_noise.luau"
echo "golden values written to roblox/tests/golden"
