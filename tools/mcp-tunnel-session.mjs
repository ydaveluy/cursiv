// Usage: node tools/mcp-tunnel-session.mjs <server-binary> <workspace-folder>
// The `cursiv/mcp` tunnel over an LSP session: the diagnostics it answers after
// a didOpen with unsaved text must be the buffer's, not the disk's.

import path from 'node:path';
import { startSession } from './session-client.mjs';

const [serverPath, folderPath] = process.argv.slice(2);
if (!serverPath || !folderPath) {
  console.error('Usage: node tools/mcp-tunnel-session.mjs <server-binary> <workspace-folder>');
  process.exit(2);
}
const folder = path.resolve(folderPath);
const folderUri = 'file://' + folder;

const { send, request, end: serverEnd } = startSession(serverPath);

// The tunnel's answer is the MCP response itself, which can be an error too.
const mcp = async (message) => {
  const { result: answer } = await request(
    { method: 'cursiv/mcp', params: { jsonrpc: '2.0', ...message } }, `the tunnel's ${message.method}`);
  if (answer?.id !== message.id) throw new Error(`${message.method}: answered under id ${answer?.id}`);
  if (answer.error) throw new Error(`${message.method}: ${answer.error.code}: ${answer.error.message}`);
  return answer.result;
};

let failed = false;
try {
  await request({
    method: 'initialize',
    params: {
      processId: process.pid,
      rootUri: null,
      capabilities: { workspace: { workspaceFolders: true } },
      workspaceFolders: [{ uri: folderUri, name: 'workspace' }],
    },
  }, 'initialize');
  send({ method: 'initialized', params: {} });

  const listed = await mcp({ id: 100, method: 'tools/list' });
  console.log('tools: ' + listed.tools.map((t) => t.name).join(', '));

  // An unsaved buffer, read through the tunnel.
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
  const diagnostics = await mcp({
    id: 101, method: 'tools/call',
    params: { name: 'get-diagnostics', arguments: {} },
  });
  const text = diagnostics.content[0].text;
  console.log('--- get-diagnostics (with the unsaved buffer) ---');
  console.log(text.trimEnd());
  if (diagnostics.isError) {
    failed = true;
    console.error('FAIL: get-diagnostics answered a tool error');
  }
  if (!text.includes('unsaved.dmodel') || !text.includes('NeverOnDisk')) {
    failed = true;
    console.error('FAIL: the unsaved buffer did not reach the MCP door');
  }

  await request({ method: 'shutdown' }, 'shutdown');
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
