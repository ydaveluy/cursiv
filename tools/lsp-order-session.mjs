#!/usr/bin/env node
// A request is answered from the notifications that preceded it. The client
// types -- `didChange` -- and asks, without waiting: the answer must be the
// document it just wrote, not the one before. The two travel different paths
// inside the server, and only the order they were sent in ties them.
import { startSession } from './session-client.mjs';

const [serverPath, folder] = process.argv.slice(2);
if (!serverPath || !folder) {
  console.error('usage: lsp-order-session.mjs <server> <folder>');
  process.exit(2);
}

const { send, waitFor, end: serverEnd } = startSession(serverPath);

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

const end = await serverEnd();
if (end !== 'code 0') {
  failed = true;
  console.error(`FAIL: the server ended with ${end}`);
}
process.exit(failed ? 1 : 0);
