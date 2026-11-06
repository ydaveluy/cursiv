// Usage: node tools/mcp-tunnel-session.mjs <server-binary> <workspace-folder>
//
// Drives the `cursiv/mcp` tunnel over a real LSP session: handshake, then an
// MCP tools/call through the tunnel, then the acceptance criterion itself -- a
// didOpen with unsaved text, and the diagnostics the MCP door answers must be
// the buffer's, not the disk's. Exits 0 when both hold and the server then
// exits cleanly.

import path from 'node:path';
import { startSession } from './session-client.mjs';

const [serverPath, folderPath] = process.argv.slice(2);
if (!serverPath || !folderPath) {
  console.error('Usage: node tools/mcp-tunnel-session.mjs <server-binary> <workspace-folder>');
  process.exit(2);
}
const folder = path.resolve(folderPath);
const folderUri = 'file://' + folder;

const { send, waitFor, end: serverEnd } = startSession(serverPath);
const answerTo = (id, what) => waitFor((m) => m.id === id, what);

const mcp = async (id, message) => {
  send({ id, method: 'cursiv/mcp', params: { jsonrpc: '2.0', ...message } });
  const answer = await answerTo(id, message.method);
  if (answer.error) throw new Error(`tunnel error: ${answer.error.message}`);
  return answer.result;
};

let failed = false;
try {
  send({
    id: 1, method: 'initialize',
    params: {
      processId: process.pid,
      rootUri: null,
      capabilities: { workspace: { workspaceFolders: true } },
      workspaceFolders: [{ uri: folderUri, name: 'workspace' }],
    },
  });
  const init = await answerTo(1, 'initialize');
  if (init.error) throw new Error(`initialize failed: ${init.error.message}`);
  send({ method: 'initialized', params: {} });

  const listed = await mcp(2, { id: 100, method: 'tools/list' });
  console.log('tools: ' + listed.result.tools.map((t) => t.name).join(', '));

  // The acceptance criterion: an unsaved buffer, readable through the door.
  const uri = folderUri + '/unsaved.dmodel';
  send({
    method: 'textDocument/didOpen',
    params: {
      textDocument: {
        uri, languageId: 'domain-model', version: 1,
        text: 'entity Unsaved { y : NeverOnDisk }\n',
      },
    },
  });
  const diagnostics = await mcp(3, {
    id: 101, method: 'tools/call',
    params: { name: 'get-diagnostics', arguments: {} },
  });
  const text = diagnostics.result.content[0].text;
  console.log('--- get-diagnostics (with the unsaved buffer) ---');
  console.log(text.trimEnd());
  if (!text.includes('unsaved.dmodel') || !text.includes('NeverOnDisk')) {
    failed = true;
    console.error('FAIL: the unsaved buffer did not reach the MCP door');
  }

  send({ id: 9, method: 'shutdown' });
  await answerTo(9, 'shutdown');
  send({ method: 'exit' });
} catch (error) {
  console.error(String(error));
  failed = true;
}

const end = await serverEnd();
if (end !== 'code 0') {
  failed = true;
  console.error(`FAIL: the server ended with ${end}`);
}
process.exit(failed ? 1 : 0);
