#!/usr/bin/env bash
# Complete the "Try it online" page of a documentation build. MkDocs builds
# the page from docs/demo/ and copies its scripts; this adds the Monaco editor
# and coi-serviceworker from npm, the example documents, and the language
# servers that tools/wasm-dist.sh collects from a wasi-release build. The page
# needs http(s): serve the site over https or on localhost.
#
#   tools/build-demo.sh <servers-dir> <site-demo-dir>   # e.g. dist site/demo
set -euo pipefail

[ $# = 2 ] || { echo "usage: $0 <servers-dir> <site-demo-dir>" >&2; exit 2; }
servers=$(cd "$1" && pwd)
[ -f "$2/index.html" ] || { echo "no page in $2: run mkdocs build first" >&2; exit 1; }
out=$(cd "$2" && pwd)
root=$(cd "$(dirname "$0")/.." && pwd)
demo=$root/docs/demo

# npm ci is the reproducible install, outside the docs so MkDocs never copies
# the packages. They are plain files: none of their scripts runs.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$demo"/package.json "$demo"/package-lock.json "$work/"
(cd "$work" && npm ci --ignore-scripts --no-audit --no-fund --silent)
npm=$work/node_modules

cp "$npm/coi-serviceworker/coi-serviceworker.js" "$out/"
mkdir -p "$out/licenses"

# Monaco: the AMD build, trimmed to the editor itself.
for part in loader.js editor/editor.main.js editor/editor.main.css \
            base/worker/workerMain.js base/browser/ui/codicons/codicon/codicon.ttf; do
  mkdir -p "$out/vendor/monaco/vs/$(dirname "$part")"
  cp "$npm/monaco-editor/min/vs/$part" "$out/vendor/monaco/vs/$part"
done

cp "$npm/monaco-editor/LICENSE" "$out/licenses/monaco-LICENSE"
cp "$npm/monaco-editor/ThirdPartyNotices.txt" "$out/licenses/monaco-ThirdPartyNotices.txt"
cp "$npm/coi-serviceworker/LICENSE" "$out/licenses/coi-serviceworker.LICENSE"
cp "$servers"/{cursiv,lsp-framework,mimalloc}.LICENSE "$out/licenses/"

# Each server and example document at the path languages.json gives it.
node - "$demo/languages.json" "$servers" "$root/examples" "$out" <<'NODE'
const fs = require('node:fs');
const path = require('node:path');
const [list, servers, examples, out] = process.argv.slice(2);
const copy = (from, to) => {
  fs.mkdirSync(path.dirname(to), { recursive: true });
  fs.copyFileSync(from, to);
};
for (const lang of JSON.parse(fs.readFileSync(list, 'utf8'))) {
  copy(path.join(servers, path.basename(lang.server)), path.join(out, lang.server));
  for (const doc of lang.documents)
    copy(path.join(examples, lang.id, 'example', doc.name), path.join(out, doc.path));
  console.log(`${lang.id.padEnd(16)} ${(fs.statSync(path.join(out, lang.server)).size / 1024) | 0} KiB`);
}
NODE
