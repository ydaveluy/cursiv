#!/usr/bin/env bash
# Everything one F5 needs, for one example: the server and the bundled client.
# The `preLaunchTask` of every "Run <X> Extension" configuration; the steps are
# the same for every language, so one script serves them all.
set -euo pipefail

key=${1:?usage: prepare.sh <example>}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

# The active preset's tree only: the task passes what the editor has selected,
# so a native preset runs the host binary and a wasi one the wasm module.
# Configuring is the preset's job; this only builds.
build=${CURSIV_BUILD_DIR:-$root/build}
if [ ! -f "$build/CMakeCache.txt" ]; then
  echo "prepare.sh: $build is not configured -- select a preset, or run" \
       "\`cmake --preset gcc-release\`" >&2
  exit 1
fi
cmake --build "$build" --target "${key}_lsp" --parallel

# Each client is its own npm package, installed from its lock file again when
# npm's record of the install is older than the manifest or the lock file.
cd "$root/examples/$key/vscode"
installed=node_modules/.package-lock.json
if [ ! "$installed" -nt package.json ] || [ ! "$installed" -nt package-lock.json ]; then
  npm ci --no-audit --no-fund
fi
npm run compile
