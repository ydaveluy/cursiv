// Usage: node tools/mcp-session.mjs <server-binary> [--folder <dir>]
//                                    [--call <tool> <argsJson>]...
//
// Drives a cursiv MCP session over stdio (one JSON message per line):
// initialize, tools/list, get-diagnostics, then every requested call. Exits 0
// when the handshake completed and every call answered without error. Native
// binaries only.

import { spawn } from 'node:child_process';
import { StringDecoder } from 'node:string_decoder';

const argv = process.argv.slice(2);
let folder = '.';
const calls = [];
const rest = [];
for (let i = 0; i < argv.length; ++i) {
  if (argv[i] === '--folder') { folder = argv[++i]; continue; }
  if (argv[i] === '--call') { calls.push([argv[++i], argv[++i] ?? '{}']); continue; }
  rest.push(argv[i]);
}
const [serverPath] = rest;
if (!serverPath) {
  console.error('Usage: node tools/mcp-session.mjs <server-binary> [--folder <dir>] [--call <tool> <argsJson>]...');
  process.exit(2);
}

const child = spawn(serverPath, ['--mcp', '--folder', folder],
                    { stdio: ['pipe', 'pipe', 'inherit'] });

// A StringDecoder, not chunk.toString: a character straddling a pipe-read
// boundary would decode to U+FFFD in both halves.
const decoder = new StringDecoder('utf8');
let buffer = '';
const received = [];
child.stdout.on('data', (chunk) => {
  buffer += decoder.write(chunk);
  let nl;
  while ((nl = buffer.indexOf('\n')) !== -1) {
    const line = buffer.slice(0, nl);
    buffer = buffer.slice(nl + 1);
    if (line.trim()) received.push(JSON.parse(line));
  }
});

const send = (message) =>
  child.stdin.write(JSON.stringify({ jsonrpc: '2.0', ...message }) + '\n');

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

let failed = false;
try {
  send({
    id: 1, method: 'initialize',
    params: {
      protocolVersion: '2025-06-18', capabilities: {},
      clientInfo: { name: 'mcp-session', version: '0' },
    },
  });
  const init = await waitFor(1, 'initialize');
  console.log(`server: ${init.result.serverInfo.name} ${init.result.serverInfo.version}` +
              ` (protocol ${init.result.protocolVersion})`);
  send({ method: 'notifications/initialized' });

  send({ id: 2, method: 'tools/list' });
  const tools = await waitFor(2, 'tools/list');
  console.log('tools: ' + tools.result.tools.map((t) => t.name).join(', '));

  send({ id: 3, method: 'tools/call',
         params: { name: 'get-diagnostics', arguments: {} } });
  const diagnostics = await waitFor(3, 'get-diagnostics');
  if (diagnostics.error) {
    throw new Error('get-diagnostics: ' + diagnostics.error.message);
  }
  if (diagnostics.result.isError) failed = true;
  console.log('--- get-diagnostics ---');
  console.log(diagnostics.result.content[0].text.trimEnd());

  let id = 4;
  for (const [name, args] of calls) {
    send({ id, method: 'tools/call',
           params: { name, arguments: JSON.parse(args) } });
    const answer = await waitFor(id, name);
    console.log(`--- ${name} ---`);
    if (answer.error) {
      failed = true;
      console.log('protocol error: ' + answer.error.message);
    } else {
      if (answer.result.isError) failed = true;
      console.log(answer.result.content[0].text.trimEnd());
    }
    ++id;
  }
} catch (error) {
  console.error(String(error));
  failed = true;
}

child.stdin.end();
await new Promise((resolve) => {
  const timer = setTimeout(() => { child.kill(); resolve(); }, 2000);
  child.on('close', () => { clearTimeout(timer); resolve(); });
});
process.exit(failed ? 1 : 0);
