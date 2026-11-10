// One MCP session over stdio: initialize, tools/list, get-diagnostics, then
// each `--call`. Exits 0 when every call answered without error and the server
// exited cleanly.

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

const { child, request, send, end: serverEnd } =
  startSession(serverPath, ['--mcp', '--folder', folder], { lines: true });

let failed = false;
try {
  const init = await request({
    method: 'initialize',
    params: {
      protocolVersion: '2025-06-18', capabilities: {},
      clientInfo: { name: 'mcp-session', version: '0' },
    },
  }, 'initialize');
  console.log(`server: ${init.result.serverInfo.name} ${init.result.serverInfo.version}` +
              ` (protocol ${init.result.protocolVersion})`);
  send({ method: 'notifications/initialized' });

  const tools = await request({ method: 'tools/list' }, 'tools/list');
  console.log('tools: ' + tools.result.tools.map((t) => t.name).join(', '));

  // A protocol error is the request failing; `isError` is the tool's own
  // failure, answered in the content.
  for (const [name, args] of [['get-diagnostics', '{}'], ...calls]) {
    const answer = await request({ method: 'tools/call',
                                   params: { name, arguments: JSON.parse(args) } }, name);
    console.log(`--- ${name} ---`);
    if (answer.result.isError) failed = true;
    console.log(answer.result.content[0].text.trimEnd());
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
