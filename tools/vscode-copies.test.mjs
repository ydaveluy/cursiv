// The example extensions are copies that may differ only by their language,
// and the template shares their MCP bridge and bundle script. Their sources
// also run here, over stand-ins for VS Code and its client.
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import * as module from 'node:module';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const examples = new URL('../examples/', import.meta.url);
const template = new URL('../templates/project/vscode/', import.meta.url);
const languages = ['arithmetics', 'domainmodel', 'requirements', 'statemachine'];
const copies = ['src/extension.ts', 'src/mcp.ts', 'esbuild.mjs', 'tsconfig.json'];
const templateCopies = ['src/mcp.ts', 'esbuild.mjs'];

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
  const andTemplate = templateCopies.includes(file) ? ' and the project template' : '';
  test(`${file} is the same in every example extension${andTemplate}`, () => {
    const [first, ...others] = languages;
    const expected = normalized(first, file);
    for (const language of others)
      assert.equal(normalized(language, file), expected,
        `examples/${language}/vscode/${file} differs from examples/${first}/vscode/${file}`);
    if (templateCopies.includes(file))
      assert.equal(readFileSync(new URL(file, template), 'utf8'), expected,
        `templates/project/vscode/${file} differs from examples/${first}/vscode/${file}`);
  });
}

// What the extension sources import from the editor, and what they hand it.
const standIns = {
  vscode: `
    export class CancellationTokenSource {
      token = { isCancellationRequested: false };
      cancel() { this.token.isCancellationRequested = true; }
      dispose() {}
    }
    export class Disposable { constructor(dispose) { this.dispose = dispose; } }
    export const Uri = { parse: (text) => text };
    export const workspace = { getConfiguration: () => ({ get: () => undefined }) };
    export const window = { createOutputChannel: () => ({ appendLine() {}, dispose() {} }),
                            showErrorMessage() {} };`,
  'vscode-languageclient/node': `
    export const started = [];
    export const TransportKind = { stdio: 0 };
    export class LanguageClient {
      constructor(id, name, serverOptions) { this.serverOptions = serverOptions; }
      async start() { started.push(this); }
      async stop() {}
    }`,
};
const standIn = (name) => `data:text/javascript,${encodeURIComponent(standIns[name])}`;
// Type stripping and synchronous hooks: Node.js 22.18 and later.
const runsSources = Boolean(process.features.typescript && module.registerHooks);
if (runsSources) {
  module.registerHooks({
    resolve(specifier, context, next) {
      if (specifier in standIns) return { url: standIn(specifier), shortCircuit: true };
      // The sources import each other as a bundler does, without extensions.
      if (specifier === './mcp') return next('./mcp.ts', context);
      return next(specifier, context);
    },
  });
}
const skip = !runsSources && 'this Node.js runs no TypeScript';

test('the MCP bridge answers the requests it holds when it closes', { skip }, async () => {
  const { startMcpBridge } = await import(new URL('domainmodel/vscode/src/mcp.ts', examples));
  const lines = [];
  let reached;
  const asked = new Promise((resolve) => { reached = resolve; });
  // A client whose server stopped answering: its request never settles.
  const client = { sendRequest: () => { reached(); return new Promise(() => {}); } };
  const bridge = startMcpBridge('x', 'X', client, { appendLine: (line) => lines.push(line) });
  while (!lines.length) await new Promise((wake) => setTimeout(wake, 5));
  const [, url, token] = /^mcp: (\S+) \(Authorization: Bearer (\S+)\)$/.exec(lines[0]);
  // Given up on after a while: an answer that never comes must not hold the test.
  const answer = fetch(url, { method: 'POST', headers: { Authorization: `Bearer ${token}`,
                                'Mcp-Method': 'tools/list', 'MCP-Protocol-Version': '2026-07-28' },
                              body: JSON.stringify({ jsonrpc: '2.0', id: 3, method: 'tools/list', params: { _meta: {
                                'io.modelcontextprotocol/protocolVersion': '2026-07-28',
                                'io.modelcontextprotocol/clientCapabilities': {},
                              } } }),
                              signal: AbortSignal.timeout(2000) });
  await asked;
  bridge.dispose();
  const response = await answer;
  assert.equal(response.status, 503);
  assert.equal((await response.json()).id, 3);
});

test('the MCP bridge validates routing headers and propagates protocol errors', { skip }, async () => {
  const { startMcpBridge } = await import(new URL('domainmodel/vscode/src/mcp.ts', examples));
  let ready;
  const listening = new Promise(resolve => { ready = resolve; });
  const called = [];
  const client = { async sendRequest(_, msg) {
    if (msg.protocolVersion) {
      assert.equal(msg.protocolVersion, '2025-11-25');
      assert.equal(msg.message.method, 'ping');
      return { result: {} };
    }
    called.push(msg.method);
    if (msg.method === 'tools/list') return { result: { tools: [{ name: 'echo', inputSchema: {
      properties: { nested: { properties: { label: { type: 'string', 'x-mcp-header': 'Label' } } } },
    } }] } };
    if (msg.method === 'tools/call') return { result: { content: [] } };
    return { error: { code: msg.method === 'unknown' ? -32601 : -32022 } };
  } };
  const bridge = startMcpBridge('x', 'X', client, { appendLine: ready });
  try {
    const [, url, token] = /^mcp: (\S+) \(Authorization: Bearer (\S+)\)$/.exec(await listening);
    const version = '2026-07-28';
    const post = (method, overrides, params = {}) => fetch(url, {
      method: 'POST', headers: { Authorization: `Bearer ${token}`, 'Mcp-Method': method,
        'MCP-Protocol-Version': version, ...overrides },
      body: JSON.stringify({ jsonrpc: '2.0', id: 7, method, params: { _meta: {
        'io.modelcontextprotocol/protocolVersion': version, 'io.modelcontextprotocol/clientCapabilities': {},
      }, ...params } }),
    });
    let response = await post('tools/list', { 'Mcp-Method': 'tools/call' });
    assert.equal(response.status, 400);
    assert.equal((await response.json()).error.code, -32020);
    assert.deepEqual(called, []);
    const params = { name: 'echo', arguments: { nested: { label: 'été' } } };
    response = await post('tools/call', { 'Mcp-Name': 'echo' }, params);
    assert.equal((await response.json()).error.code, -32020);
    assert.deepEqual(called, ['tools/list']);
    const encoded = `=?base64?${Buffer.from('été').toString('base64')}?=`;
    response = await post('tools/call', { 'Mcp-Name': 'echo', 'Mcp-Param-Label': encoded }, params);
    assert.equal(response.status, 200);
    assert.deepEqual(called, ['tools/list', 'tools/call']);
    assert.equal((await post('unknown')).status, 404);
    assert.equal((await post('server/discover')).status, 400);
    response = await fetch(url, { method: 'POST', headers: { Authorization: `Bearer ${token}`,
      'Mcp-Protocol-Version': '2025-11-25', 'Mcp-Method': 'ping' },
      body: JSON.stringify({ jsonrpc: '2.0', id: 7, method: 'ping' }) });
    assert.equal(response.status, 200);
    assert.deepEqual((await response.json()).result, {});
  } finally { bridge.dispose(); }
});

// The extension the way VS Code starts it, on a wasm server the active preset
// built: the language server's environment is the editor's own.
for (const [what, folder] of [['the example extensions', new URL('domainmodel/vscode/', examples)],
                              ['the project template', template]]) {
  test(`the worker count the editor was given reaches the server of ${what}`, { skip }, async () => {
    const extension = await import(new URL('src/extension.ts', folder));
    const { started } = await import(standIn('vscode-languageclient/node'));
    const server = /^const SERVER = '(.*)';$/m.exec(readFileSync(new URL('src/extension.ts', folder), 'utf8'))[1];
    const directory = mkdtempSync(join(tmpdir(), 'cursiv-extension-'));
    const saved = { ...process.env };
    const context = { extensionPath: fileURLToPath(folder), subscriptions: [] };
    try {
      writeFileSync(join(directory, process.platform === 'win32' ? `${server}.exe` : server), '\0asm');
      // The template runs the runner it packages; the examples run the repository's.
      mkdirSync(join(directory, 'server'));
      writeFileSync(join(directory, 'server', 'wasi-run.mjs'), '');
      if (folder === template) context.extensionPath = directory;
      Object.assign(process.env, { CURSIV_SERVER_DIR: directory, CURSIV_WORKERS: '0' });
      await extension.activate(context);
      const { run } = started.at(-1).serverOptions;
      assert.match(run.args.join(' '), /wasi-run[.]mjs/);
      assert.equal((run.options?.env ?? process.env).CURSIV_WORKERS, '0');
    } finally {
      context.subscriptions.forEach((d) => d.dispose());
      await extension.deactivate();
      process.env = saved;
      rmSync(directory, { recursive: true, force: true });
    }
  });
}
