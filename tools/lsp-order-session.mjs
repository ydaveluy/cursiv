#!/usr/bin/env node
// A request is answered from the notifications that preceded it. The client
// types -- `didChange` -- and asks, without waiting: the answer must be the
// document it just wrote, not the one before. The two travel different paths
// inside the server, and only the order they were sent in ties them.
import { spawn } from 'node:child_process';

const [serverPath, folder] = process.argv.slice(2);
if (!serverPath || !folder) {
  console.error('usage: lsp-order-session.mjs <server> <folder>');
  process.exit(2);
}

const child = spawn(serverPath, [], { stdio: ['pipe', 'pipe', 'inherit'] });
const send = (message) => {
  const body = JSON.stringify({ jsonrpc: '2.0', ...message });
  child.stdin.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
};

let buffer = Buffer.alloc(0);
const received = [];
child.stdout.on('data', (chunk) => {
  buffer = Buffer.concat([buffer, chunk]);
  for (;;) {
    const headerEnd = buffer.indexOf('\r\n\r\n');
    if (headerEnd === -1) return;
    const header = buffer.subarray(0, headerEnd).toString('utf8');
    const length = Number(/content-length:\s*(\d+)/i.exec(header)?.[1]);
    if (buffer.length < headerEnd + 4 + length) return;
    received.push(JSON.parse(
        buffer.subarray(headerEnd + 4, headerEnd + 4 + length).toString('utf8')));
    buffer = buffer.subarray(headerEnd + 4 + length);
  }
});

const waitFor = (predicate, what, timeoutMs = 20000) =>
  new Promise((resolve, reject) => {
    const started = Date.now();
    const poll = () => {
      const found = received.find(predicate);
      if (found) return resolve(found);
      if (child.exitCode !== null)
        return reject(new Error(`server exited before answering ${what}`));
      if (Date.now() - started > timeoutMs)
        return reject(new Error(`timed out waiting for ${what}`));
      setTimeout(poll, 5);
    };
    poll();
  });

const uri = `file://${folder}/order.dmodel`;
let failed = false;
try {
  send({ id: 1, method: 'initialize', params: {
    processId: process.pid, rootUri: null, capabilities: {},
    workspaceFolders: [{ uri: `file://${folder}`, name: 'workspace' }] } });
  await waitFor((m) => m.id === 1, 'initialize');
  send({ method: 'initialized', params: {} });

  send({ method: 'textDocument/didOpen', params: { textDocument: {
    uri, languageId: 'domain-model', version: 1,
    text: 'entity Alpha { }\n' } } });
  await waitFor((m) => m.method === 'textDocument/publishDiagnostics' &&
                       m.params.uri === uri, 'the first diagnostics');

  // Settled. Now a keystroke, and the question right behind it.
  send({ method: 'textDocument/didChange', params: {
    textDocument: { uri, version: 2 },
    contentChanges: [{ text: 'entity Beta { }\nentity Gamma { }\n' }] } });
  send({ id: 2, method: 'textDocument/documentSymbol',
         params: { textDocument: { uri } } });
  const answer = await waitFor((m) => m.id === 2, 'the documentSymbol result');
  const names = (answer.result ?? []).map((s) => s.name).join(',');
  console.log(`documentSymbol after one keystroke: ${names || '(none)'}`);
  if (names !== 'Beta,Gamma') {
    failed = true;
    console.error(`FAIL: answered from the document before the change`);
  }

  send({ id: 9, method: 'shutdown' });
  await waitFor((m) => m.id === 9, 'shutdown');
  send({ method: 'exit' });
} catch (error) {
  failed = true;
  console.error(String(error && error.message ? error.message : error));
}

await new Promise((resolve) => {
  const timer = setTimeout(() => { child.kill(); resolve(); }, 2000);
  child.on('exit', () => { clearTimeout(timer); resolve(); });
});
process.exit(failed ? 1 : 0);
