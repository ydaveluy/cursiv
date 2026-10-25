#!/usr/bin/env bash
# Everything one F5 needs, for one example: the server and the bundled client.
# The `preLaunchTask` of every "Run <X> Extension" configuration; the steps are
# the same for every language, so one script serves them all.
set -euo pipefail

key=${1:?usage: prepare.sh <example>}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

# THE ACTIVE PRESET'S TREE, and only it: the task passes what the editor has
# selected, so a native preset runs the host binary and a wasi one the wasm
# module. Configuring is the preset's own business; this only builds.
build=${CURSIV_BUILD_DIR:-$root/build}
if [ ! -f "$build/CMakeCache.txt" ]; then
  echo "prepare.sh: $build is not configured -- select a preset, or run" \
       "\`cmake --preset gcc-release\`" >&2
  exit 1
fi
cmake --build "$build" --target "${key}_lsp" --parallel

# Each client is its own npm package -- no workspace root exists above them.
cd "$root/examples/$key/vscode"
[ -d node_modules ] || npm install --no-audit --no-fund
npm run compile
