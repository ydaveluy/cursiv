#!/usr/bin/env bash
# The installable extensions: one .vsix per language, each embedding the
# optimized wasm server and the Node runner under server/ -- an installed
# extension has no repository tree to fall back on, so the module travels
# with it. Needs a wasi-threads build; SDK Clang optimizes Release executables,
# and this script packages them without another optimization pass.
#
#   tools/package-vsix.sh [build-dir] [out-dir]   # default: build-wasi dist-vsix
set -euo pipefail

build=${1:-build-wasi}
out=${2:-dist-vsix}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

mkdir -p "$out"
out=$(cd "$out" && pwd)
tools/wasm-dist.sh "$build" "$out/modules"

staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT

for ext in examples/*/vscode; do
  key=$(basename "$(dirname "$ext")")
  extension=$(cd "$ext" && pwd)
  module="$out/modules/${key}_lsp.wasm"
  [ -f "$module" ] || { echo "no module for $key under $out/modules"; exit 1; }

  (
    cd "$extension"
    # npm ci is the reproducible install, and it wants the lock file; an
    # already-populated node_modules is reused as-is.
    if [ ! -d node_modules ]; then
      if [ -f package-lock.json ]; then npm ci --silent; else npm install --silent; fi
    fi
    npm run bundle
  )

  stage="$staging/$key"
  mkdir -p "$stage/server"
  cp "$extension/package.json" "$extension/language-configuration.json" "$stage/"
  cp -R "$extension/out" "$extension/syntaxes" "$stage/"
  cp "$module" tools/wasi-run.mjs "$stage/server/"
  for notice in cursiv.LICENSE lsp-framework.LICENSE mimalloc.LICENSE; do
    cp "$out/modules/$notice" "$stage/server/"
  done

  # WASI uses the extension host's Node 22 runtime. Keep native manifests and
  # their lock files unchanged; only these distributed WASI packages need it.
  node - "$stage/package.json" <<'NODE'
const fs = require('node:fs');
const file = process.argv[2];
const manifest = JSON.parse(fs.readFileSync(file, 'utf8'));
manifest.engines.vscode = '^1.101.0';
delete manifest.scripts['vscode:prepublish'];
fs.writeFileSync(file, JSON.stringify(manifest, null, 2) + '\n');
NODE
  filename=$(node -e 'const p = require(process.argv[1]); console.log(`${p.name}-${p.version}.vsix`);' "$stage/package.json")
  vsix="$out/$filename"
  (
    cd "$stage"
    "$extension/node_modules/.bin/vsce" package --no-dependencies \
      --allow-missing-repository --skip-license -o "$vsix" >/dev/null
  )
  printf '%-24s %8d KiB\n' "$key" "$(($(stat -c%s "$vsix") / 1024))"
done
