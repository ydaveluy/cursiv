import assert from 'node:assert/strict';
import { chmodSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { startSession } from './session-client.mjs';

// A server written in a few lines of JavaScript, run by this same node. It
// reads LSP frames and hands each message to `onMessage`; `write` frames one.
const framed = String.raw`
let input = Buffer.alloc(0);
const write = (m) => {
  const body = JSON.stringify({ jsonrpc: '2.0', ...m });
  process.stdout.write('Content-Length: ' + Buffer.byteLength(body) + '\r\n\r\n' + body);
};
process.stdin.on('data', (chunk) => {
  input = Buffer.concat([input, chunk]);
  for (;;) {
    const at = input.indexOf('\r\n\r\n');
    if (at < 0) return;
    const length = Number(/Content-Length: (\d+)/.exec(input.subarray(0, at).toString())[1]);
    if (input.length < at + 4 + length) return;
    const message = JSON.parse(input.subarray(at + 4, at + 4 + length).toString());
    input = input.subarray(at + 4 + length);
    onMessage(message);
  }
});
`;
const server = (body, options) => startSession(process.execPath, ['-e', framed + body], options);

// The session's process is gone for good, whatever `end` said about it.
const assertGone = (child) =>
  assert.ok(child.exitCode !== null || child.signalCode !== null, 'the server process is still alive');

test('a clean session answers by id and ends with code 0', async () => {
  // The answer is written in two pieces, cut inside a character.
  const session = server(String.raw`
    function onMessage(m) {
      if (m.method === 'exit') process.exit(0);
      if (m.method !== 'hello') return write({ id: m.id, result: null });
      const bytes = Buffer.from(JSON.stringify({ jsonrpc: '2.0', id: m.id, result: 'é€' }));
      const head = 'Content-Length: ' + bytes.length + '\r\n\r\n';
      process.stdout.write(head + bytes.subarray(0, bytes.length - 4).toString('latin1'), 'latin1');
      setTimeout(() => process.stdout.write(bytes.subarray(bytes.length - 4)), 50);
    }`);
  assert.equal((await session.request({ method: 'hello' }, 'hello')).result, 'é€');
  await session.request({ method: 'shutdown' }, 'shutdown');
  session.send({ method: 'exit' });
  assert.equal(await session.end(), 'code 0');
});

test('an error response fails its request', async () => {
  const session = server(String.raw`
    function onMessage(m) {
      if (m.method === 'exit') process.exit(0);
      write({ id: m.id, error: { code: -32603, message: 'no' } });
    }`);
  await assert.rejects(session.request({ method: 'shutdown' }, 'shutdown'), /shutdown: -32603: no/);
  session.send({ method: 'exit' });
  assert.equal(await session.end(), 'code 0');
});

test('a request goes under its fresh id, whatever id its message holds', async () => {
  const session = server(String.raw`
    function onMessage(m) {
      if (m.method === 'exit') process.exit(0);
      write({ id: m.id, result: m.id });
    }`);
  try {
    assert.equal((await session.request({ id: 42, method: 'ask' }, 'ask', { timeoutMs: 2000 })).result, 1);
  } finally {
    session.send({ method: 'exit' });
  }
  assert.equal(await session.end(), 'code 0');
});

test('a request the server sends under the same id answers nothing', async () => {
  const session = server(String.raw`
    function onMessage(m) {
      if (m.method === 'exit') process.exit(0);
      write({ id: m.id, method: 'workspace/configuration', params: { items: [] } });
      setTimeout(() => write({ id: m.id, result: 'mine' }), 200);
    }`);
  assert.equal((await session.request({ method: 'ask' }, 'ask')).result, 'mine');
  session.send({ method: 'exit' });
  assert.equal(await session.end(), 'code 0');
});

test('a server that dies fails the wait for its answer', async () => {
  const session = server('function onMessage() { process.exit(3); }');
  await assert.rejects(session.request({ method: 'ask' }, 'ask'), /server exited before answering ask/);
  assert.equal(await session.end(), 'code 3');
});

test('a server deaf to SIGTERM is killed before end resolves', async () => {
  const session = server(String.raw`
    process.on('SIGTERM', () => {});
    setInterval(() => {}, 1000);
    function onMessage() {}`);
  try {
    // Let the server start and set its SIGTERM handler before `end` stops waiting.
    session.send({ method: 'ping' });
    await new Promise((resolve) => setTimeout(resolve, 500));
    const end = await session.end();
    assert.notEqual(end, 'code 0');
    assert.match(end, /SIGKILL/);
    assertGone(session.child);
  } finally {
    session.child.kill('SIGKILL');
  }
});

test('a frame cut short at the end of the output fails the session', async () => {
  const session = server(String.raw`
    function onMessage(m) {
      if (m.method === 'exit') process.exit(0);
      write({ id: m.id, result: null });
      process.stdout.write('Content-Length: 50\r\n\r\n{"jsonrpc"');
    }`);
  await session.request({ method: 'shutdown' }, 'shutdown');
  session.send({ method: 'exit' });
  assert.match(await session.end(), /^code 0; the output ended inside a message/);
});

test('a line cut short at the end of the output fails the session', async () => {
  const session = startSession(process.execPath, ['-e', String.raw`
    process.stdin.on('data', () => {});
    process.stdin.on('end', () => process.stdout.write('{"jsonrpc":"2.0","id":1,"result":1}\n{"jsonrpc"'));
  `], { lines: true });
  session.child.stdin.end();
  assert.match(await session.end(), /^code 0; the output ended inside a line/);
  assert.equal(session.received.length, 1);
});

for (const [header, error] of [
  ['X-Content-Length: 2', /no Content-Length/],
  ['Content-Length: 2abc', /bad Content-Length/],
  ['Content-Length: 2\r\nContent-Length: 2', /two Content-Length headers/],
  ['Content-Length: 99999999999999999999', /past what a buffer holds/],
]) {
  test(`a header block ${JSON.stringify(header)} fails the session at once`, async () => {
    const session = server(`function onMessage() { process.stdout.write(${JSON.stringify(header + '\r\n\r\n{}')}); }`);
    const started = Date.now();
    await assert.rejects(session.request({ method: 'ask' }, 'ask', { timeoutMs: 10000 }), error);
    assert.ok(Date.now() - started < 5000, 'the wait ran to its timeout');
    session.child.stdin.end();
    assert.match(await session.end(), error);
    assertGone(session.child);
  });
}

test('a message that is not JSON fails the session', async () => {
  const session = server(String.raw`function onMessage() { process.stdout.write('Content-Length: 3\r\n\r\n{x}'); }`);
  await assert.rejects(session.request({ method: 'ask' }, 'ask'), /not JSON/);
  session.child.stdin.end();
  assert.match(await session.end(), /^code 0; a message that is not JSON/);
});

test('a predicate that throws fails its wait', async () => {
  const session = server(String.raw`function onMessage(m) { setTimeout(() => write({ id: m.id, result: null }), 100); }`);
  session.send({ id: 1, method: 'ask' });
  await assert.rejects(session.waitFor(() => { throw new Error('bad predicate'); }, 'ask'), /bad predicate/);
  session.child.stdin.end();
  assert.equal(await session.end(), 'code 0');
  assertGone(session.child);
});

test('a server that stops reading its input fails the wait at once', async () => {
  const session = startSession(process.execPath, ['-e', String.raw`
    require('node:fs').closeSync(0);
    setInterval(() => {}, 1000);
  `]);
  await new Promise((resolve) => setTimeout(resolve, 300));
  const started = Date.now();
  await assert.rejects(session.request({ method: 'ask' }, 'ask', { timeoutMs: 10000 }),
                       /writing to the server failed/);
  assert.ok(Date.now() - started < 5000, 'the wait ran to its timeout');
  assert.notEqual(await session.end(), 'code 0');
  assertGone(session.child);
});

test('a server that cannot start fails its waits and its end', async (t) => {
  if (process.platform === 'win32') return t.skip('no execute permission bit');
  const folder = mkdtempSync(join(tmpdir(), 'cursiv-session-'));
  try {
    const path = join(folder, 'server');
    writeFileSync(path, 'not a program\n');
    chmodSync(path, 0o644);
    const session = startSession(path);
    await assert.rejects(session.request({ method: 'ask' }, 'ask'), /server exited|the server failed/);
    assert.match(await session.end(), /the server failed/);
  } finally {
    rmSync(folder, { recursive: true, force: true });
  }
});
