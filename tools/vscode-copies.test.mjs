// The example extensions are copies of one extension: each folder packages on
// its own, so they cannot share the files, but they may differ only by the
// language they serve. A fix made in one copy and not the others fails here.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

const examples = new URL('../examples/', import.meta.url);
const languages = ['arithmetics', 'domainmodel', 'requirements', 'statemachine'];
const copies = ['src/extension.ts', 'src/mcp.ts', 'esbuild.mjs', 'tsconfig.json'];

// The copy with its language's names replaced by placeholders: the folder
// name, as written, capitalized and upper case, and the list of language ids.
function normalized(language, file) {
  const text = readFileSync(new URL(`${language}/vscode/${file}`, examples), 'utf8');
  const label = language[0].toUpperCase() + language.slice(1);
  return text
    .replace(/^const LANGUAGES = \[.*\];$/m, 'const LANGUAGES = @LANGUAGES@;')
    .replaceAll(language, '@ID@')
    .replaceAll(label, '@LABEL@')
    .replaceAll(language.toUpperCase(), '@UPPER@');
}

for (const file of copies) {
  test(`${file} is the same in every example extension`, () => {
    const [first, ...others] = languages;
    const expected = normalized(first, file);
    for (const language of others)
      assert.equal(normalized(language, file), expected,
        `examples/${language}/vscode/${file} differs from examples/${first}/vscode/${file}`);
  });
}
