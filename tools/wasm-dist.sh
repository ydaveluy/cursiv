#!/usr/bin/env bash
# Collect the LSP modules and their notices in dist/. The WASI Release build
# lets SDK Clang run wasm-opt after linking; packaging copies modules as built.
#
#   tools/wasm-dist.sh [build-dir] [dist-dir]   # default: build-wasi dist
set -euo pipefail

build=${1:-build-wasi}
dist=${2:-dist}

mkdir -p "$dist"
for notice in cursiv.LICENSE lsp-framework.LICENSE mimalloc.LICENSE; do
  source="$build/cursiv-licenses/$notice"
  [ -f "$source" ] || {
    echo "missing $source; configure the threaded WASI build again" >&2
    exit 1
  }
  cp "$source" "$dist/"
done
found=0
for bin in "$build"/examples/*/src/*_lsp; do
  [ -f "$bin" ] || continue
  found=1
  name=$(basename "$bin").wasm
  cp "$bin" "$dist/$name"
  printf '%-24s %8d KiB\n' "$name" "$(($(stat -c%s "$dist/$name") / 1024))"
done
[ "$found" = 1 ] || { echo "no *_lsp modules under $build/examples"; exit 1; }
