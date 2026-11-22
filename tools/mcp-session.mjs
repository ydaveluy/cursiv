// One MCP session over stdio: discover, tools/list, get-diagnostics, then
// each `--call`. Exits 0 when every call answered without error and the server
// exited cleanly.

import { resolve } from 'node:path';
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

const { child, request, end: serverEnd } =
  startSession(serverPath, ['--mcp', '--folder', resolve(folder)], { lines: true });

let failed = false;
try {
  const call = (method, params = {}) => request({ method, params: { ...params, _meta: {
    'io.modelcontextprotocol/protocolVersion': '2026-07-28',
    'io.modelcontextprotocol/clientCapabilities': {},
  } } }, method);
  const { result } = await call('server/discover');
  const info = result._meta['io.modelcontextprotocol/serverInfo'];
  console.log(`server: ${info.name} ${info.version} (protocol ${result.supportedVersions.join(', ')})`);

  const tools = await call('tools/list');
  console.log('tools: ' + tools.result.tools.map((t) => t.name).join(', '));

  // A protocol error is the request failing; `isError` is the tool's own
  // failure, answered in the content.
  for (const [name, args] of [['get-diagnostics', '{}'], ...calls]) {
    const answer = await call('tools/call', { name, arguments: JSON.parse(args) });
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
