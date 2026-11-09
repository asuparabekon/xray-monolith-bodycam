#!/usr/bin/env bash
# Builds the original C++ Bodycam code and regenerates roblox/tests/golden/*.luau.
# Needs CMake and a C++17 compiler. Run from anywhere.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
build="${BUILD_DIR:-$repo/roblox/tools/golden/build}"
mkdir -p "$build/reference"
cmake -S "$repo/src/xrGame/bodycam_reference" -B "$build/reference" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build/reference" -j >/dev/null
lib="$(find "$build/reference" -name 'libbodycam_reference.a' | head -n1)"
c++ -std=c++17 -O2 -DBODYCAM_STANDALONE \
	"$here/golden.cpp" \
	"$repo/src/xrGame/bodycam_simulation.cpp" \
	"$repo/src/xrGame/bodycam_mouse_input.cpp" \
	"$repo/src/xrGame/bodycam_viewmodel_profile.cpp" \
	"$lib" -o "$build/golden"
"$build/golden" "$repo/roblox/tests/golden"
echo "golden traces written to roblox/tests/golden"
