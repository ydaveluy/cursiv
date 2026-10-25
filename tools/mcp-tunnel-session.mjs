// Usage: node tools/mcp-tunnel-session.mjs <server-binary> <workspace-folder>
//
// Drives the `cursiv/mcp` tunnel over a real LSP session: handshake, then an
// MCP tools/call through the tunnel, then the acceptance criterion itself -- a
// didOpen with unsaved text, and the diagnostics the MCP door answers must be
// the buffer's, not the disk's. Exits 0 when both hold.

import { spawn } from 'node:child_process';
import path from 'node:path';

const [serverPath, folderPath] = process.argv.slice(2);
if (!serverPath || !folderPath) {
  console.error('Usage: node tools/mcp-tunnel-session.mjs <server-binary> <workspace-folder>');
  process.exit(2);
}
const folder = path.resolve(folderPath);
const folderUri = 'file://' + folder;

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
    const body = buffer.subarray(headerEnd + 4, headerEnd + 4 + length);
    buffer = buffer.subarray(headerEnd + 4 + length);
    received.push(JSON.parse(body.toString('utf8')));
  }
});

const waitFor = (id, what, timeoutMs = 20000) =>
  new Promise((resolve, reject) => {
    const started = Date.now();
    const poll = () => {
      const found = received.find((m) => m.id === id);
      if (found) return resolve(found);
      if (child.exitCode !== null)
        return reject(new Error(`server exited before answering ${what}`));
      if (Date.now() - started > timeoutMs)
        return reject(new Error(`timed out waiting for ${what}`));
      setTimeout(poll, 20);
    };
    poll();
  });

const mcp = async (id, message) => {
  send({ id, method: 'cursiv/mcp', params: { jsonrpc: '2.0', ...message } });
  const answer = await waitFor(id, message.method);
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
  const init = await waitFor(1, 'initialize');
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
  await waitFor(9, 'shutdown');
  send({ method: 'exit' });
} catch (error) {
  console.error(String(error));
  failed = true;
}

await new Promise((resolve) => {
  const timer = setTimeout(() => { child.kill(); resolve(); }, 2000);
  child.on('close', () => { clearTimeout(timer); resolve(); });
});
process.exit(failed ? 1 : 0);
