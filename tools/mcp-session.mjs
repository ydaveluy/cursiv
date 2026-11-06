// Usage: node tools/mcp-session.mjs <server-binary> [--folder <dir>]
//                                    [--call <tool> <argsJson>]...
//
// Drives a cursiv MCP session over stdio (one JSON message per line):
// initialize, tools/list, get-diagnostics, then every requested call. Exits 0
// when the handshake completed, every call answered without error and the
// server exited cleanly at the end of its input.

import { startSession } from './session-client.mjs';

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

const { child, send, waitFor, end: serverEnd } =
  startSession(serverPath, ['--mcp', '--folder', folder], { lines: true });
const answerTo = (id, what) => waitFor((m) => m.id === id, what);

let failed = false;
try {
  send({
    id: 1, method: 'initialize',
    params: {
      protocolVersion: '2025-06-18', capabilities: {},
      clientInfo: { name: 'mcp-session', version: '0' },
    },
  });
  const init = await answerTo(1, 'initialize');
  console.log(`server: ${init.result.serverInfo.name} ${init.result.serverInfo.version}` +
              ` (protocol ${init.result.protocolVersion})`);
  send({ method: 'notifications/initialized' });

  send({ id: 2, method: 'tools/list' });
  const tools = await answerTo(2, 'tools/list');
  console.log('tools: ' + tools.result.tools.map((t) => t.name).join(', '));

  send({ id: 3, method: 'tools/call',
         params: { name: 'get-diagnostics', arguments: {} } });
  const diagnostics = await answerTo(3, 'get-diagnostics');
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
    const answer = await answerTo(id, name);
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
const end = await serverEnd();
if (end !== 'code 0') {
  failed = true;
  console.error(`FAIL: the server ended with ${end}`);
}
process.exit(failed ? 1 : 0);
