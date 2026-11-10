// The online demo's scripts: the worker on real language servers, the page
// transport and the Monaco adapter over stand-ins.
//   node tools/demo-worker.test.mjs <module>...
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { basename } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { Worker } from 'node:worker_threads';
import { importedMemoryLimits } from './wasi-run.mjs';
import { startServer } from '../docs/demo/server.js';
import { diagnosedDocument, registerLanguageFeatures } from '../docs/demo/lsp.js';

const script = fileURLToPath(new URL('../docs/demo/wasi-worker.js', import.meta.url));

// The globals of a browser's worker, over a Node one, with stacks written as
// Firefox writes them: frames only, without the error's message. With `race`,
// a thread is handed to the pool worker right after its first look at its slot.
const host = `
  const { parentPort } = require('node:worker_threads');
  globalThis.postMessage = (message) => parentPort.postMessage(message);
  Error.prepareStackTrace = (error, frames) => frames.map((f) => '@' + f).join('\\n');
  require('node:vm').runInThisContext(
      require('node:fs').readFileSync(${JSON.stringify(script)}, 'utf8'));
  parentPort.once('message', (data) => {
    if (data.race) {
      const load = Atomics.load;
      let armed = true;
      Atomics.load = (array, index) => {
        const value = load(array, index);
        if (armed && array === data.ctl && index === 2 && value === 0) {
          armed = false;
          Atomics.store(array, index, 2);
          Atomics.notify(array, index);
        }
        return value;
      };
    }
    globalThis.onmessage({ data });
  });
`;

// A pool worker in the shape `docs/demo/server.js` starts it, and what it says.
async function poolWorker(bytes, limits, { race = false, tid = 0, arg = 0 } = {}) {
  const shared = (n) => new SharedArrayBuffer(n);
  const worker = new Worker(host, { eval: true });
  const said = [];
  const waiting = [];
  worker.on('message', (m) => { said.push(m); waiting.splice(0).forEach((wake) => wake()); });
  const next = async (ms = 5000) => {
    const deadline = Date.now() + ms;
    while (!said.length) {
      if (Date.now() > deadline) return { type: 'nothing' };
      await new Promise((wake) => { waiting.push(wake); setTimeout(wake, 50); });
    }
    return said.shift();
  };
  const spawnArgs = new Int32Array(shared(8));
  spawnArgs[0] = tid; spawnArgs[1] = arg;
  const ctl = new Int32Array(shared(12));
  ctl[1] = -1;
  ctl[2] = 3;   // busy until the worker is ready
  worker.postMessage({
    role: 'thread', slot: 0, root: null, files: {}, env: {}, race,
    module: await WebAssembly.compile(bytes),
    memory: new WebAssembly.Memory({ ...limits, shared: true }),
    ctl, spawnArgs,
    stdinHdr: new Int32Array(shared(12)), stdinBuf: new Uint8Array(shared(16)),
  });
  return { worker, next, ctl };
}

async function firstWord(bytes, limits) {
  const { worker, next } = await poolWorker(bytes, limits);
  try {
    return await next();
  } finally {
    await worker.terminate();
  }
}

for (const path of process.argv.slice(2)) {
  test(`${basename(path)} has every import it asks for`, async () => {
    const bytes = readFileSync(path);
    const word = await firstWord(bytes, importedMemoryLimits(bytes));
    assert.equal(word.type, 'ready', word.text);
  });
}

// Hand-written modules, section by section.
const u32 = (value) => {
  const bytes = [];
  do {
    const part = value % 128;
    value = Math.floor(value / 128);
    bytes.push(part | (value ? 128 : 0));
  } while (value);
  return bytes;
};
const name = (text) => [...u32(Buffer.byteLength(text)), ...Buffer.from(text)];
const section = (id, ...entries) => {
  const body = [...u32(entries.length), ...entries.flat()];
  return [id, ...u32(body.length), ...body];
};
const wasm = (...sections) => new Uint8Array([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0, ...sections.flat()]);

// `wasi_thread_start(tid, arg)` calls `proc_exit(arg)`: a thread says it ran.
const threadModule = wasm(
  section(1, [0x60, 1, 0x7f, 0], [0x60, 2, 0x7f, 0x7f, 0]),
  section(2, [...name('wasi_snapshot_preview1'), ...name('proc_exit'), 0, 0],
             [...name('env'), ...name('memory'), 2, 3, 1, 1]),
  section(3, [1]),
  section(7, [...name('wasi_thread_start'), 0, 1]),
  section(10, [6, 0, 0x20, 1, 0x10, 0, 0x0b]));

test('a module that cannot start says why', async () => {
  const bytes = wasm(
    section(1, [0x60, 0, 0]),
    section(2, [...name('wasi_snapshot_preview1'), ...name('no_such_call'), 0, 0]));
  const word = await firstWord(bytes, { initial: 1, maximum: 1 });
  assert.equal(word.type, 'crash');
  // The page shows the first line only.
  assert.match(word.text.split('\n')[0], /no_such_call/);
});

for (const race of [false, true]) {
  test(`a pool worker starts the thread it is handed${race ? ' between two looks at its slot' : ''}`, async () => {
    const { worker, next, ctl } = await poolWorker(threadModule, { initial: 1, maximum: 1 },
                                                   { race, tid: 7, arg: 42 });
    try {
      assert.equal((await next()).type, 'ready');
      if (!race) {
        while (Atomics.load(ctl, 2) !== 0) await new Promise((wake) => setTimeout(wake, 5));
        Atomics.store(ctl, 2, 2);
        Atomics.notify(ctl, 2);
      }
      assert.deepEqual(await next(), { type: 'exit', code: 42 });
    } finally {
      await worker.terminate();
    }
  });
}

// The worker's functions, called in this thread over the test's memory.
function workerScope(ctx) {
  const scope = vm.createContext({ postMessage() {}, TextDecoder, TextEncoder, performance, crypto, ctx });
  vm.runInContext(readFileSync(script, 'utf8'), scope);
  vm.runInContext('ctx = globalThis.ctx', scope);
  return scope;
}

test('stdin reads on past 2 GiB in total and across the ring\'s end', () => {
  const memory = new WebAssembly.Memory({ initial: 1 });
  const stdinHdr = new Int32Array(new SharedArrayBuffer(12));
  const stdinBuf = new Uint8Array(new SharedArrayBuffer(16));
  const scope = workerScope({ memory, stdinHdr, stdinBuf });
  // 2^31 + 12 bytes went through; 8 more wait where the page put them, the
  // last 4 back at the ring's start.
  const total = 2 ** 31 + 12;
  for (let k = 0; k < 8; ++k) stdinBuf[(total + k) % 16] = k + 1;
  stdinHdr[1] = total | 0;
  stdinHdr[0] = (total + 8) | 0;
  const view = new DataView(memory.buffer);
  view.setUint32(32, 64, true);   // two iovecs: 3 bytes at 64, 16 at 80
  view.setUint32(36, 3, true);
  view.setUint32(40, 80, true);
  view.setUint32(44, 16, true);
  assert.equal(scope.readStdin(32, 2, 0), 0);
  assert.equal(view.getUint32(0, true), 8);
  assert.deepEqual([...new Uint8Array(memory.buffer, 64, 3)], [1, 2, 3]);
  assert.deepEqual([...new Uint8Array(memory.buffer, 80, 6)], [4, 5, 6, 7, 8, 0]);
  assert.equal(stdinHdr[1], (total + 8) | 0);
});

test('a poll reports the clocks that are due, not the others', () => {
  const memory = new WebAssembly.Memory({ initial: 1 });
  const scope = workerScope({ memory });
  const view = new DataView(memory.buffer);
  // Two relative clocks on the monotonic clock: 1 ms (user data 1), one hour (2).
  for (const [i, ns] of [[0, 1e6], [1, 3600e9]]) {
    const s = 256 + 48 * i;
    view.setBigUint64(s, BigInt(i + 1), true);
    view.setUint8(s + 8, 0);
    view.setUint32(s + 16, 1, true);
    view.setBigUint64(s + 24, BigInt(ns), true);
    view.setUint16(s + 40, 0, true);
  }
  assert.equal(scope.pollOneoff(256, 512, 2, 0), 0);
  assert.equal(view.getUint32(0, true), 1);
  assert.equal(view.getBigUint64(512, true), 1n);
  assert.equal(view.getUint16(512 + 8, true), 0);
});

test('a poll answers an unsupported subscription at once, beside a later clock', () => {
  const memory = new WebAssembly.Memory({ initial: 1 });
  const scope = workerScope({ memory });
  const view = new DataView(memory.buffer);
  // A read on a descriptor (user data 1), a clock with no such id (2), and two
  // seconds on the monotonic clock (3).
  for (const [i, type, clock, ns] of [[0, 1, 0, 0], [1, 0, 9, 0], [2, 0, 1, 2e9]]) {
    const s = 256 + 48 * i;
    view.setBigUint64(s, BigInt(i + 1), true);
    view.setUint8(s + 8, type);
    view.setUint32(s + 16, clock, true);
    view.setBigUint64(s + 24, BigInt(ns), true);
    view.setUint16(s + 40, 0, true);
  }
  const start = performance.now();
  assert.equal(scope.pollOneoff(256, 512, 3, 0), 0);
  assert.ok(performance.now() - start < 1000, 'the clock was not waited for');
  assert.equal(view.getUint32(0, true), 2);
  assert.deepEqual([0, 1].map((e) => [view.getBigUint64(512 + 32 * e, true),
                                      view.getUint16(512 + 32 * e + 8, true)]),
                   [[1n, 52], [2n, 28]]);   // ENOSYS, then EINVAL
});

test('the clocks are the wall and monotonic ones, and no other', () => {
  const memory = new WebAssembly.Memory({ initial: 1 });
  const { clock_time_get: now } = workerScope({ memory, env: {} }).wasiImports();
  assert.equal(now(0, 0n, 0), 0);
  assert.equal(now(1, 0n, 0), 0);
  assert.equal(now(2, 0n, 0), 58, 'no CPU clock: ENOTSUP');
  assert.equal(now(3, 0n, 0), 58);
  assert.equal(now(4, 0n, 0), 28, 'no such clock: EINVAL');
});

test('pointers past 2 GiB are read as unsigned', () => {
  // The module passes an i32: an address of 2 GiB or more arrives negative.
  const memory = new WebAssembly.Memory({ initial: 32769 });
  const imports = workerScope({ memory, env: { A: 'b' } }).wasiImports();
  const high = 2 ** 31, signed = (address) => address | 0;
  const view = new DataView(memory.buffer);
  assert.equal(imports.clock_time_get(1, 0n, signed(high)), 0);
  assert.notEqual(view.getBigUint64(high, true), 0n);
  assert.equal(imports.environ_get(signed(high + 8), signed(high + 16)), 0);
  assert.equal(view.getUint32(high + 8, true), high + 16);
  assert.equal(Buffer.from(memory.buffer, high + 16, 4).toString(), 'A=b\0');
  assert.equal(imports.random_get(signed(high + 32), 16), 0);
  assert.ok(new Uint8Array(memory.buffer, high + 32, 16).some((b) => b !== 0));
});

test('a folder lists its entries from a cookie, cut off where the buffer ends', () => {
  const memory = new WebAssembly.Memory({ initial: 1 });
  const files = { '/demo/a/x.txt': new Uint8Array(1), '/demo/a/b/y.txt': new Uint8Array(1) };
  const scope = workerScope({ memory, root: '/demo', files, env: {} });
  vm.runInContext('nextFd = 16', scope);
  const mem = new Uint8Array(memory.buffer), view = new DataView(memory.buffer);
  mem.set(Buffer.from('a'), 100);
  assert.equal(scope.wasiImports().path_open(3, 0, 100, 1, 2, 0n, 0n, 0, 0), 0);
  const fd = view.getUint32(0, true);
  // The records from `cookie` on: the next cookie, an inode, the name's length,
  // the type, then the name.
  const records = (cookie) => [['.', 3], ['..', 3], ['x.txt', 4], ['b', 3]].slice(cookie)
    .flatMap(([name, type], i) => {
      const record = Buffer.alloc(24 + name.length);
      record.writeBigUInt64LE(BigInt(cookie + i + 1), 0);
      record.writeBigUInt64LE(BigInt(cookie + i + 1), 8);
      record.writeUInt32LE(name.length, 16);
      record[20] = type;
      record.write(name, 24);
      return [...record];
    });
  for (const [cookie, room] of [[0, 40], [2, 4096], [4, 4096], [0, 0]]) {
    mem.fill(0xee, 1024, 1024 + 4096);
    assert.equal(scope.readDir(fd, 1024, room, BigInt(cookie), 8), 0);
    const expected = records(cookie).slice(0, room);
    assert.equal(view.getUint32(8, true), expected.length);
    assert.deepEqual([...mem.subarray(1024, 1024 + expected.length + 1)], [...expected, 0xee]);
  }
});

// The page's side of the transport, over stand-in workers the test drives.
class PageWorker extends EventTarget {
  static all = [];
  // Whether the browser refuses to create (`create`) or to start (`post`) the
  // worker of this index.
  static refuses = () => false;
  constructor() {
    super();
    if (PageWorker.refuses(PageWorker.all.length, 'create')) throw new Error('too many workers');
    PageWorker.all.push(this);
  }
  postMessage(data) {
    if (PageWorker.refuses(PageWorker.all.indexOf(this), 'post')) throw new Error('cannot clone');
    this.data = data;
    if (data.role === 'thread') this.say({ type: 'ready' });
  }
  say(data) { this.dispatchEvent(new MessageEvent('message', { data })); }
  terminate() { this.terminated = true; }
}

async function pageSession(url, { bytes = threadModule, refuses = () => false } = {}) {
  Object.assign(globalThis, { crossOriginIsolated: true, Worker: PageWorker,
    fetch: async () => ({ ok: true, arrayBuffer: async () => bytes.buffer }) });
  PageWorker.all = [];
  PageWorker.refuses = refuses;
  const exits = [];
  const server = await startServer(url, { onExit: (reason) => exits.push(reason) });
  const main = PageWorker.all.find((w) => w.data.role === 'main');
  // What the page wrote into the stdin ring, read the way the worker reads it.
  const written = () => {
    const { stdinHdr, stdinBuf } = main.data;
    const head = Atomics.load(stdinHdr, 0), tail = Atomics.load(stdinHdr, 1);
    const bytes = [];
    for (let k = 0; k < ((head - tail) >>> 0); ++k) bytes.push(stdinBuf[(tail + k) & (stdinBuf.length - 1)]);
    return Buffer.from(bytes).toString();
  };
  const reply = (text) => main.say({ type: 'out', fd: 1, bytes: new Uint8Array(Buffer.from(text)) });
  return { server, exits, written, reply, stdinHdr: main.data.stdinHdr };
}
const frame = (body, header = '') =>
  `Content-Length: ${Buffer.byteLength(body)}\r\n${header}\r\n${body}`;
const settled = (promise) => Promise.race([
  promise.then((value) => ({ value }), (error) => ({ error })),
  new Promise((wake) => setTimeout(() => wake({ pending: true }), 500))]);

test('the page writes stdin on past 2 GiB in total and across the ring\'s end', async () => {
  const { server, written, stdinHdr } = await pageSession('ring');
  // 10 bytes short of a lap of the 1 MiB ring.
  stdinHdr.fill((2 ** 31 + 2 ** 20 - 10) | 0, 0, 2);
  server.notify('initialized', {});
  assert.equal(written(), frame(JSON.stringify({ jsonrpc: '2.0', method: 'initialized', params: {} })));
  server.dispose();
});

test('a request is cancelled until its answer comes, not after', async () => {
  const { server, written, reply } = await pageSession('cancel');
  const listeners = new Set();
  const token = { onCancellationRequested(fn) {
    listeners.add(fn);
    return { dispose: () => listeners.delete(fn) };
  } };
  const cancel = () => listeners.forEach((fn) => fn());
  const cancelFrame = (id) =>
    frame(JSON.stringify({ jsonrpc: '2.0', method: '$/cancelRequest', params: { id } }));
  const answered = server.request('textDocument/hover', {}, token);
  reply(frame('{"jsonrpc":"2.0","id":1,"result":null}'));
  assert.deepEqual(await settled(answered), { value: null });
  const before = written();
  cancel();
  assert.equal(written(), before, 'an answered request is not cancelled');
  const pending = server.request('textDocument/hover', {}, token);
  cancel();
  assert.ok(written().endsWith(cancelFrame(2)));
  server.dispose();
  assert.ok((await settled(pending)).error);
  assert.equal(listeners.size, 0, 'no request listens once the session stopped');
});

test('a frame with a long header still answers', async () => {
  const { server, reply } = await pageSession('long-header');
  const answer = server.request('initialize', {});
  reply(frame('{"jsonrpc":"2.0","id":1,"result":{"ok":true}}', `Content-Type: ${'x'.repeat(300)}\r\n`));
  assert.deepEqual(await settled(answer), { value: { ok: true } });
  server.dispose();
});

// The module's memory is one page: no message it writes can be longer.
for (const [what, bad] of [['no length', 'Content-Type: x\r\n\r\n{}'],
                           ['a body that is no JSON', frame('{"id":')],
                           ['a length past the server\'s memory', 'Content-Length: 65537\r\n\r\n{}'],
                           ['a length of 300 digits', `Content-Length: 1${'0'.repeat(300)}\r\n\r\n{}`]]) {
  test(`a frame with ${what} ends the session`, async () => {
    const { server, exits, reply } = await pageSession(what);
    const answer = server.request('initialize', {});
    reply(bad);
    const result = await settled(answer);
    assert.ok(result.error, 'the request is refused');
    assert.equal(server.running, false);
    assert.match(exits[0] ?? '', /server sent a message the page cannot read/);
  });
}

for (const step of ['create', 'post']) {
  test(`a start the browser refuses to ${step} a worker for stops the others`, async () => {
    let refused = false;
    const refuses = (index, at) => at === step && index === 2 && !refused && (refused = true);
    await assert.rejects(pageSession(`refused ${step}`, { refuses }));
    assert.ok(PageWorker.all.length >= 3);
    assert.equal(PageWorker.all.filter((w) => !w.terminated).length, 0, 'a worker is left running');
  });
}

test('a module with two imported memories is refused', async () => {
  const twoMemories = wasm(section(2, [...name('env'), ...name('memory'), 2, 3, 1, 1],
                                      [...name('env'), ...name('other'), 2, 3, 1, 1]));
  await assert.rejects(pageSession('two memories', { bytes: twoMemories }), /one shared memory/);
});

// The Monaco adapter, over an editor that records what it is given.
function editorStandIn() {
  class Range {
    constructor(a, b, c, d) {
      Object.assign(this, { startLineNumber: a, startColumn: b, endLineNumber: c, endColumn: d });
    }
  }
  const providers = {};
  const languages = new Proxy({ CompletionItemKind: { Text: 0, Keyword: 17 },
                                CompletionItemInsertTextRule: { InsertAsSnippet: 4 } }, {
    get: (known, key) => key in known ? known[key]
      : (_id, provider) => { providers[key.replace(/^register|Provider$/g, '')] = provider; },
  });
  return { providers, monaco: { Range, languages, Uri: { parse: (uri) => ({ uri, toString: () => uri }) },
                                Emitter: class { event() {} fire() {} } } };
}
function adapter(answers, documentVersion = 3) {
  const { providers, monaco } = editorStandIn();
  const uri = 'file:///demo/a.x';
  const model = { uri: { toString: () => uri }, getVersionId: () => 17,
                  getWordUntilPosition: () => ({ startColumn: 1, endColumn: 1 }) };
  const session = {
    server: { running: true, request: async (method) => answers[method] },
    capabilities: {}, documents: [{ uri, model, version: documentVersion }],
  };
  registerLanguageFeatures(monaco, 'x', () => session, () => []);
  return { providers, model, uri };
}
const textEdit = { range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } }, newText: 'b' };
const position = { lineNumber: 1, column: 1 };

test('an edit made against a document version holds the editor to it', async () => {
  const uri = 'file:///demo/a.x';
  const action = (version, extra = []) => ({ title: 'fix', edit: { documentChanges: [
    { textDocument: { uri, version }, edits: [textEdit] }, ...extra] } });
  const actionsFor = async (answer, version) => {
    const { providers, model } = adapter({ 'textDocument/codeAction': answer }, version);
    const range = { startLineNumber: 1, startColumn: 1, endLineNumber: 1, endColumn: 1 };
    return (await providers.CodeAction.provideCodeActions(model, range, {}, {})).actions;
  };
  const [fresh] = await actionsFor([action(3)], 3);
  assert.equal(fresh.edit.edits[0].versionId, 17, 'applied only while the model is unchanged');
  assert.deepEqual(await actionsFor([action(3)], 4), [], 'the document changed since');
  assert.deepEqual(await actionsFor([action(3, [{ kind: 'create', uri: 'file:///demo/b.x' }])], 3), [],
                   'this page creates no file');
  assert.deepEqual(await actionsFor([action(3, [{ textDocument: { uri: 'file:///demo/b.x', version: 1 },
                                                 edits: [textEdit] }])], 3), [],
                   'a version of a document the editor does not hold');

  const { providers, model } = adapter({ 'textDocument/rename': action(2).edit }, 3);
  const renamed = await providers.Rename.provideRenameEdits(model, position, 'b', {});
  assert.deepEqual(renamed.edits, []);
  assert.ok(renamed.rejectReason);
});

test('diagnostics apply to the version they were made for', () => {
  const uri = 'file:///demo/a.x', documents = [{ uri, version: 3 }];
  assert.equal(diagnosedDocument(documents, { uri, version: 3, diagnostics: [] }), documents[0]);
  assert.equal(diagnosedDocument(documents, { uri, version: 2, diagnostics: [] }), undefined,
               'the document changed since');
  assert.equal(diagnosedDocument(documents, { uri, diagnostics: [] }), documents[0],
               'a publication without a version, such as a removal');
  assert.equal(diagnosedDocument(documents, { uri: 'file:///demo/b.x', diagnostics: [] }), undefined);
});

test('plain text reads as written', async () => {
  const plain = { kind: 'plaintext', value: 'a*b* [x](y) &copy; <b>' };
  const { providers, model } = adapter({
    'textDocument/hover': { contents: plain },
    'textDocument/completion': [{ label: 'k', documentation: plain }],
  });
  const hover = await providers.Hover.provideHover(model, position, {});
  // Markdown reads entities and drops HTML tags: both are escaped too.
  assert.deepEqual(hover.contents,
                   [{ value: 'a\\*b\\*&nbsp;\\[x\\]\\(y\\)&nbsp;&amp;copy;&nbsp;&lt;b\\>' }]);
  const { suggestions } = await providers.CompletionItem.provideCompletionItems(model, position, {}, {});
  assert.equal(suggestions[0].documentation, plain.value);
});

test('a completion brings its other edits and its command', async () => {
  const { providers, model } = adapter({ 'textDocument/completion': [{
    label: 'k', additionalTextEdits: [textEdit],
    command: { title: 'again', command: 'editor.action.triggerSuggest' } }] });
  const { suggestions: [item] } = await providers.CompletionItem.provideCompletionItems(model, position, {}, {});
  assert.deepEqual(item.additionalTextEdits.map((e) => e.text), ['b']);
  assert.deepEqual(item.command, { id: 'editor.action.triggerSuggest', title: 'again', arguments: undefined });
});
