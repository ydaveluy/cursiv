#!/usr/bin/env bash
# The editor of the "Try it online" page: Monaco bundled from npm with esbuild,
# with coi-serviceworker and their licences. tools/build-demo.sh runs it before
# adding the language servers; the documentation check runs it alone, so a
# Monaco update that breaks the bundle fails before it reaches main.
#
#   tools/bundle-demo-editor.sh <site-demo-dir>   # e.g. site/demo
set -euo pipefail

[ $# = 1 ] || { echo "usage: $0 <site-demo-dir>" >&2; exit 2; }
[ -f "$1/index.html" ] || { echo "no page in $1: run mkdocs build first" >&2; exit 1; }
out=$(cd "$1" && pwd)
root=$(cd "$(dirname "$0")/.." && pwd)
demo=$root/docs/demo

# npm ci is the reproducible install, outside the docs so MkDocs never copies
# the packages. None of their install scripts runs: esbuild works without its
# own.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$demo"/package.json "$demo"/package-lock.json "$demo"/monaco.js "$work/"
(cd "$work" && npm ci --ignore-scripts --no-audit --no-fund --silent)
npm=$work/node_modules

# A service worker must be a file of its own.
cp "$npm/coi-serviceworker/coi-serviceworker.js" "$out/"
mkdir -p "$out/licenses"

# Monaco: the editor of docs/demo/monaco.js and its worker, with the CSS and
# the icon font they use.
"$npm/.bin/esbuild" --bundle --minify --format=esm --log-level=warning \
  --loader:.ttf=file --asset-names='[name]' --outdir="$out/vendor/monaco" \
  "monaco=$work/monaco.js" \
  "editor.worker=$npm/monaco-editor/esm/vs/editor/editor.worker.js"

cp "$npm/monaco-editor/LICENSE" "$out/licenses/monaco-LICENSE"
cp "$npm/monaco-editor/ThirdPartyNotices.txt" "$out/licenses/monaco-ThirdPartyNotices.txt"
cp "$npm/coi-serviceworker/LICENSE" "$out/licenses/coi-serviceworker.LICENSE"
