#!/usr/bin/env node
// A request is answered after the notifications sent before it: a request
// right behind a `didChange` sees the changed document.
import { startSession } from './session-client.mjs';

const [serverPath, folder] = process.argv.slice(2);
if (!serverPath || !folder) {
  console.error('usage: lsp-order-session.mjs <server> <folder>');
  process.exit(2);
}

const { send, waitFor, request, end: serverEnd } = startSession(serverPath);

const uri = `file://${folder}/order.dmodel`;

let failed = false;
try {
  await request({ method: 'initialize', params: {
    processId: process.pid, rootUri: null, capabilities: {},
    workspaceFolders: [{ uri: `file://${folder}`, name: 'workspace' }] } }, 'initialize');
  send({ method: 'initialized', params: {} });

  send({ method: 'textDocument/didOpen', params: { textDocument: {
    uri, languageId: 'domain-model', version: 1,
    text: 'entity Alpha { }\n' } } });
  await waitFor((m) => m.method === 'textDocument/publishDiagnostics' &&
                       m.params.uri === uri && m.params.version === 1, 'the first diagnostics');

  // Once settled, a keystroke and the request right behind it.
  send({ method: 'textDocument/didChange', params: {
    textDocument: { uri, version: 2 },
    contentChanges: [{ text: 'entity Beta { }\nentity Gamma { }\n' }] } });
  const answer = await request({ method: 'textDocument/documentSymbol',
                                 params: { textDocument: { uri } } }, 'the documentSymbol result');
  const names = (answer.result ?? []).map((s) => s.name).join(',');
  console.log(`documentSymbol after one keystroke: ${names || '(none)'}`);
  if (names !== 'Beta,Gamma') {
    failed = true;
    console.error(`FAIL: answered from the document before the change`);
  }

  await request({ method: 'shutdown' }, 'shutdown');
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
