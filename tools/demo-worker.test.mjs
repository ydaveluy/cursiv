// The online demo's worker, run on real language servers: it must give a module
// every import it asks for, and say so when one cannot start. Usage:
//   node tools/demo-worker.test.mjs <module>...
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { basename } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { Worker } from 'node:worker_threads';
import { importedMemoryLimits } from './wasi-run.mjs';

const script = fileURLToPath(new URL('../docs/demo/wasi-worker.js', import.meta.url));

// The globals of a browser's worker, over a Node one.
const host = `
  const { parentPort } = require('node:worker_threads');
  globalThis.postMessage = (message) => parentPort.postMessage(message);
  require('node:vm').runInThisContext(
      require('node:fs').readFileSync(${JSON.stringify(script)}, 'utf8'));
  parentPort.once('message', (data) => globalThis.onmessage({ data }));
`;

// What a pool worker says first, in the shape `docs/demo/server.js` starts it.
async function firstWord(bytes, limits) {
  const shared = (n) => new SharedArrayBuffer(n);
  const worker = new Worker(host, { eval: true });
  try {
    const answer = new Promise((resolve, reject) => {
      worker.once('message', resolve);
      worker.once('error', reject);
    });
    worker.postMessage({
      role: 'thread', slot: 0, root: null, files: {}, env: {},
      module: await WebAssembly.compile(bytes),
      memory: new WebAssembly.Memory({ ...limits, shared: true }),
      ctl: new Int32Array(shared(12)), spawnArgs: new Int32Array(shared(8)),
      stdinHdr: new Int32Array(shared(12)), stdinBuf: new Uint8Array(shared(16)),
    });
    return await answer;
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

test('a module that cannot start says why', async () => {
  const name = [...Buffer.from('wasi_snapshot_preview1')];
  const call = [...Buffer.from('no_such_call')];
  const bytes = new Uint8Array([
    0, 0x61, 0x73, 0x6d, 1, 0, 0, 0,
    1, 4, 1, 0x60, 0, 0,  // one type: () -> ()
    2, 2 + name.length + 1 + call.length + 2, 1,  // one import, a function
    name.length, ...name, call.length, ...call, 0, 0,
  ]);
  const word = await firstWord(bytes, { initial: 1, maximum: 1 });
  assert.equal(word.type, 'crash');
  assert.match(word.text, /no_such_call/);
});
