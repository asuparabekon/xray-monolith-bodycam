#!/bin/bash
# Builds the svg_to_vector command-line tool natively (g++), from the engine's vector reticle sources.
#   bash tools/reticle_vector/build-linux.sh [out binary]   (default: tools/reticle_vector/svg_to_vector)
# -ffp-contract=off: no fused multiply-add, as the engine's own build (MSVC, strict floating point), so a .dds
# built here holds the same bytes as the texture the engine builds from the same SVG.
# -Wno-maybe-uninitialized: GCC's false positive in pugixml's document move.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
src="$here/../../src/Layers/xrRender/reticle_vector"
out="${1:-$here/svg_to_vector}"
g++ -std=c++17 -O2 -ffp-contract=off -Wall -Wextra -Wno-maybe-uninitialized -o "$out" "$here/svg_to_vector.cpp" \
	"$src/reticle_vector_svg.cpp" "$src/reticle_vector_build.cpp" "$here/../../src/3rd party/pugixml/pugixml.cpp"
echo "$out"
