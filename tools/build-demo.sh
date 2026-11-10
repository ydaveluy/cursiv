#!/usr/bin/env bash
# Completes the "Try it online" page of a documentation build with the editor,
# the example documents and the servers tools/wasm-dist.sh collects. Serve the
# site over https or on localhost.
#
#   tools/build-demo.sh <servers-dir> <site-demo-dir>   # e.g. dist site/demo
set -euo pipefail

[ $# = 2 ] || { echo "usage: $0 <servers-dir> <site-demo-dir>" >&2; exit 2; }
servers=$(cd "$1" && pwd)
[ -f "$2/index.html" ] || { echo "no page in $2: run mkdocs build first" >&2; exit 1; }
out=$(cd "$2" && pwd)
root=$(cd "$(dirname "$0")/.." && pwd)
demo=$root/docs/demo

"$root/tools/bundle-demo-editor.sh" "$out"
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
