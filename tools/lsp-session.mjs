#!/usr/bin/env node
// One scripted session against a server binary, over the editor's own stdio
// transport: initialize, a didOpen per document, diagnostics, documentSymbol,
// the optional requests below, shutdown. No editor and no extension host, so
// an F5 failure becomes attributable to the client rather than to the server.
//
// Usage: node tools/lsp-session.mjs <server-binary> <document>...
//   [--folder <directory>]...            workspace folder in the handshake:
//                                        the server scans it on `initialized`
//   [--at <line>:<char>]                 definition, then selectionRange
//   [--refs-at <line>:<char>]            references, declaration included
//   [--rename-at <line>:<char>=<name>]   prepareRename, then the WorkspaceEdit
//                                        printed file by file
//   [--hover-at <line>:<char>]           hover, with the text under its range
//   [--semantic-tokens]                  the delta array decoded back to words
//   [--folding]                          the fold ranges, with their first line
//
// Cursor coordinates are the protocol's own zero-based UTF-16, on the last
// document passed. Exits 0 when the handshake completed and every document
// got a diagnostics snapshot.

import { spawn } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';
import * as path from 'node:path';

const argv = process.argv.slice(2);
const rest = [];
const folderPaths = [];
let cursor = null;
let referencesCursor = null;
let renameCursor = null;
let wantSemanticTokens = false;
let wantFolding = false;
let hoverCursor = null;
for (let i = 0; i < argv.length; ++i) {
    if (argv[i] === '--at') {
        cursor = argv[++i];
        continue;
    }
    if (argv[i] === '--refs-at') {
        referencesCursor = argv[++i];
        continue;
    }
    if (argv[i] === '--rename-at') {
        renameCursor = argv[++i];
        continue;
    }
    if (argv[i] === '--semantic-tokens') {
        wantSemanticTokens = true;
        continue;
    }
    if (argv[i] === '--folding') {
        wantFolding = true;
        continue;
    }
    if (argv[i] === '--hover-at') {
        hoverCursor = argv[++i];
        continue;
    }
    if (argv[i] === '--folder') {
        folderPaths.push(argv[++i]);
        continue;
    }
    rest.push(argv[i]);
}
const [serverPath, ...documentPaths] = rest;

if (!serverPath || documentPaths.length === 0) {
    console.error('usage: lsp-session.mjs <server-binary> <document>... [--folder <directory>]...\n' +
        '       [--at <line>:<char>] [--refs-at <line>:<char>] [--rename-at <line>:<char>=<name>]\n' +
        '       [--hover-at <line>:<char>] [--semantic-tokens] [--folding]');
    process.exit(2);
}
if (folderPaths.some((folder) => folder === undefined)) {
    console.error('--folder takes a directory');
    process.exit(2);
}
for (const [flag, value] of [['--at', cursor], ['--hover-at', hoverCursor]]) {
    if (value !== null && !/^\d+:\d+$/.test(value)) {
        console.error(`${flag} takes <line>:<character>, both zero-based; got ${JSON.stringify(value)}`);
        process.exit(2);
    }
}
if (renameCursor !== null && !/^\d+:\d+=.+$/.test(renameCursor)) {
    console.error(`--rename-at takes <line>:<character>=<newName>; got ${JSON.stringify(renameCursor)}`);
    process.exit(2);
}

// A wasm module cannot be exec'd; it goes through the same runner ctest uses.
const magic = (await import('node:fs')).readFileSync(serverPath).subarray(0, 4);
const isWasm = magic.equals(Buffer.from([0x00, 0x61, 0x73, 0x6d]));
const runner = new URL('./wasi-run.mjs', import.meta.url).pathname;
const server = isWasm
    ? spawn(process.execPath, ['--no-warnings', runner, serverPath],
            { stdio: ['pipe', 'pipe', 'inherit'] })
    : spawn(serverPath, [], { stdio: ['pipe', 'pipe', 'inherit'] });

// One monotonic id per request, allocated where the request is sent: `waitFor`
// answers by id over a list it never clears, so a shared id answers twice.
let lastRequestId = 0;
async function request(message, what) {
    const id = ++lastRequestId;
    send({ id, ...message });
    const answer = await waitFor((m) => m.id === id, what);
    if (answer.error) {
        throw new Error(`${what}: ${answer.error.code}: ${answer.error.message}`);
    }
    return answer;
}

function send(message) {
    const body = Buffer.from(JSON.stringify({ jsonrpc: '2.0', ...message }), 'utf8');
    server.stdin.write(`Content-Length: ${body.length}\r\n\r\n`);
    server.stdin.write(body);
}

// The framing is the protocol's own: a header block, a blank line, then exactly
// Content-Length bytes. Anything else on the stream is a server bug and shows
// up here as a parse failure rather than as a hang.
const received = [];
let buffer = Buffer.alloc(0);
server.stdout.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    for (;;) {
        const headerEnd = buffer.indexOf('\r\n\r\n');
        if (headerEnd < 0) return;
        const headers = buffer.subarray(0, headerEnd).toString('ascii');
        const match = /content-length:\s*(\d+)/i.exec(headers);
        if (!match) {
            console.error(`no Content-Length in header block: ${JSON.stringify(headers)}`);
            process.exit(1);
        }
        const length = Number(match[1]);
        const start = headerEnd + 4;
        if (buffer.length < start + length) return;
        received.push(JSON.parse(buffer.subarray(start, start + length).toString('utf8')));
        buffer = buffer.subarray(start + length);
    }
});

function waitFor(predicate, what, timeoutMs = 10000) {
    const deadline = Date.now() + timeoutMs;
    return new Promise((resolve, reject) => {
        const poll = () => {
            const found = received.findLast(predicate);
            if (found) return resolve(found);
            if (Date.now() > deadline) return reject(new Error(`timed out waiting for ${what}`));
            setTimeout(poll, 20);
        };
        poll();
    });
}

function report(name, published) {
    const diagnostics = published.params.diagnostics ?? [];
    console.log(`diagnostics for ${name}: ${diagnostics.length}`);
    for (const d of diagnostics) {
        const { line, character } = d.range.start;
        console.log(`  ${line + 1}:${character + 1} [${d.severity}] ${d.message}`);
    }
}

// Five integers per token: delta line, delta column (from the previous token
// when they share a line, from line start otherwise), length, legend index,
// modifier bitset. The legend comes from the handshake.
function decodeSemanticTokens(data, legend, lines) {
    const tokens = [];
    let line = 0;
    let character = 0;
    for (let i = 0; i + 4 < data.length; i += 5) {
        const deltaLine = data[i];
        line += deltaLine;
        character = deltaLine === 0 ? character + data[i + 1] : data[i + 1];
        const length = data[i + 2];
        tokens.push({
            line,
            character,
            length,
            type: legend[data[i + 3]] ?? `#${data[i + 3]}`,
            text: (lines[line] ?? '').slice(character, character + length)
        });
    }
    return tokens;
}

try {
    // The folders go in the handshake, and the capability goes with them: a
    // server that has not been told the client supports workspace folders is
    // entitled to ignore the field.
    const workspaceFolders = folderPaths.map((folder) => ({
        uri: pathToFileURL(path.resolve(folder)).toString(),
        name: path.basename(path.resolve(folder))
    }));
    const initialize = await request({
        method: 'initialize',
        params: {
            processId: process.pid,
            rootUri: null,
            workspaceFolders: workspaceFolders.length ? workspaceFolders : null,
            capabilities: {
                workspace: { workspaceFolders: true },
                textDocument: { definition: { linkSupport: true } }
            }
        }
    }, 'the initialize result');
    const capabilities = initialize.result?.capabilities ?? {};
    const legend = capabilities.semanticTokensProvider?.legend?.tokenTypes ?? [];
    console.log(`initialize: textDocumentSync=${JSON.stringify(capabilities.textDocumentSync)} ` +
        `documentSymbolProvider=${JSON.stringify(capabilities.documentSymbolProvider)} ` +
        `definitionProvider=${JSON.stringify(capabilities.definitionProvider)} ` +
        `referencesProvider=${JSON.stringify(capabilities.referencesProvider)} ` +
        `renameProvider=${JSON.stringify(capabilities.renameProvider)} ` +
        `documentHighlightProvider=${JSON.stringify(capabilities.documentHighlightProvider)}`);
    console.log(`            foldingRangeProvider=${JSON.stringify(capabilities.foldingRangeProvider)} ` +
        `selectionRangeProvider=${JSON.stringify(capabilities.selectionRangeProvider)} ` +
        `semanticTokens.full=${JSON.stringify(capabilities.semanticTokensProvider?.full)}`);
    if (legend.length) console.log(`            legend: ${legend.join(', ')}`);
    console.log(`            hoverProvider=${JSON.stringify(capabilities.hoverProvider)} ` +
        `workspaceFolders=${JSON.stringify(capabilities.workspace?.workspaceFolders?.supported)}`);
    // The acknowledgement, and with a folder open it is where the scan happens:
    // every document the folder holds is in the workspace, built, and published
    // before anything below sends a `didOpen`.
    send({ method: 'initialized', params: {} });

    if (workspaceFolders.length) {
        const first = documentPaths[0];
        const firstUri = pathToFileURL(path.resolve(first)).toString();
        await waitFor(
            (m) => m.method === 'textDocument/publishDiagnostics' && m.params?.uri === firstUri,
            `the scan's diagnostics snapshot for ${path.basename(first)}`);
        const scanned = new Set(received
            .filter((m) => m.method === 'textDocument/publishDiagnostics')
            .map((m) => m.params?.uri));
        console.log(`workspace scan: ${scanned.size} document(s) found and built -- ` +
            [...scanned].map((uri) => path.basename(uri)).sort().join(', '));
    }

    const documents = [];
    for (const documentPath of documentPaths) {
        const uri = pathToFileURL(path.resolve(documentPath)).toString();
        const text = await readFile(documentPath, 'utf8');
        documents.push({ uri, name: path.basename(documentPath) });
        send({
            method: 'textDocument/didOpen',
            params: {
                textDocument: { uri, languageId: path.extname(documentPath).slice(1), version: 1, text }
            }
        });
        report(documents.at(-1).name, await waitFor(
            (m) => m.method === 'textDocument/publishDiagnostics' && m.params?.uri === uri,
            `a diagnostics snapshot for ${documents.at(-1).name}`));
    }

    // The last snapshot per document, which is what the client is showing
    // after every open: a document opened first is rebuilt when its
    // neighbour arrives.
    if (documents.length > 1) {
        console.log('after the whole workspace is open:');
        for (const document of documents) {
            report(document.name, await waitFor(
                (m) => m.method === 'textDocument/publishDiagnostics' && m.params?.uri === document.uri,
                `the final snapshot for ${document.name}`));
        }
    }

    const last = documents.at(-1);
    const symbols = await request(
        { method: 'textDocument/documentSymbol', params: { textDocument: { uri: last.uri } } },
        'the documentSymbol result');
    const outline = symbols.result ?? [];
    console.log(`documentSymbol on ${last.name}: ${outline.length} top-level symbol(s)` +
        (outline.length ? ` -- ${outline.map((s) => s.name).join(', ')}` : ''));

    // The reply is a list of location links: the origin span is the word under
    // the cursor, the target range is where the editor jumps.
    const lastLines = (await readFile(documentPaths.at(-1), 'utf8')).split('\n');
    if (cursor !== null) {
        const [line, character] = cursor.split(':').map(Number);
        const definition = await request({
            method: 'textDocument/definition',
            params: { textDocument: { uri: last.uri }, position: { line, character } }
        }, 'the definition result');
        const links = definition.result ?? [];
        const origin = links[0]?.originSelectionRange;
        console.log(`definition at ${line}:${character} of ${last.name}: ${links.length} link(s)` +
            (origin ? ` -- "${lastLines[origin.start.line]
                .slice(origin.start.character, origin.end.character)}"` : ''));
        for (const link of links) {
            const target = link.targetSelectionRange ?? link.targetRange;
            console.log(`  -> ${path.basename(link.targetUri)} ` +
                `${target.start.line + 1}:${target.start.character + 1}-` +
                `${target.end.line + 1}:${target.end.character + 1}`);
        }

        // The same caret, expanded: `selectionRange` answers the chain of
        // ever-larger spans the cursor sits inside, nested through `parent`.
        const selection = await request({
            method: 'textDocument/selectionRange',
            params: { textDocument: { uri: last.uri }, positions: [{ line, character }] }
        }, 'the selectionRange result');
        const chain = [];
        for (let at = (selection.result ?? [])[0]; at; at = at.parent) chain.push(at.range);
        console.log(`selectionRange at ${line}:${character} of ${last.name}: ${chain.length} step(s)`);
        for (const r of chain)
            console.log(`  ${r.start.line + 1}:${r.start.character + 1}-` +
                `${r.end.line + 1}:${r.end.character + 1}`);
    }

    // `includeDeclaration` is what "Find All References" sends: the list
    // carries the declaration and every name that reaches it.
    if (referencesCursor !== null) {
        const [line, character] = referencesCursor.split(':').map(Number);
        const found = await request({
            method: 'textDocument/references',
            params: {
                textDocument: { uri: last.uri },
                position: { line, character },
                context: { includeDeclaration: true }
            }
        }, 'the references result');
        const locations = found.result ?? [];
        console.log(`references at ${line}:${character} of ${last.name}: ${locations.length} location(s)`);
        for (const location of locations) {
            console.log(`  -> ${path.basename(location.uri)} ` +
                `${location.range.start.line + 1}:${location.range.start.character + 1}-` +
                `${location.range.end.line + 1}:${location.range.end.character + 1}`);
        }
    }

    // `prepareRename` first, in the editor's own order: a null there is the
    // server refusing, worth seeing on its own before the edit is asked for.
    if (renameCursor !== null) {
        const [where, newName] = renameCursor.split('=');
        const [line, character] = where.split(':').map(Number);
        const prepared = await request({
            method: 'textDocument/prepareRename',
            params: { textDocument: { uri: last.uri }, position: { line, character } }
        }, 'the prepareRename result');
        const span = prepared.result;
        console.log(`prepareRename at ${line}:${character} of ${last.name}: ` +
            (span ? `${span.start.line + 1}:${span.start.character + 1}-` +
                `${span.end.line + 1}:${span.end.character + 1}` : 'refused'));

        const renamed = await request({
            method: 'textDocument/rename',
            params: { textDocument: { uri: last.uri }, position: { line, character }, newName }
        }, 'the rename result');
        const changes = renamed.result?.changes ?? null;
        if (changes === null) {
            console.log(`rename to "${newName}": refused`);
        } else {
            const files = Object.keys(changes).sort();
            const total = files.reduce((n, uri) => n + changes[uri].length, 0);
            console.log(`rename to "${newName}": ${total} edit(s) in ${files.length} file(s)`);
            for (const uri of files) {
                console.log(`  ${path.basename(uri)}:`);
                for (const edit of changes[uri]) {
                    console.log(`    ${edit.range.start.line + 1}:${edit.range.start.character + 1}-` +
                        `${edit.range.end.line + 1}:${edit.range.end.character + 1} -> "${edit.newText}"`);
                }
            }
        }
    }

    // Printing both the token and its type is what makes a wrong delta
    // visible as a wrong word rather than a shifted number.
    if (wantSemanticTokens) {
        const answer = await request({
            method: 'textDocument/semanticTokens/full',
            params: { textDocument: { uri: last.uri } }
        }, 'the semanticTokens result');
        const data = answer.result?.data ?? [];
        const tokens = decodeSemanticTokens(data, legend, lastLines);
        console.log(`semanticTokens/full on ${last.name}: ${data.length} integers, ${tokens.length} token(s)`);
        for (const t of tokens)
            console.log(`  ${String(t.line + 1).padStart(3)}:${String(t.character + 1).padStart(3)} ` +
                `${t.type.padEnd(9)} ${JSON.stringify(t.text)}`);
    }

    // Each pair is a block the editor can collapse; `kind` is present only
    // where the server said what sort of block it is.
    if (wantFolding) {
        const answer = await request({
            method: 'textDocument/foldingRange',
            params: { textDocument: { uri: last.uri } }
        }, 'the foldingRange result');
        const ranges = answer.result ?? [];
        console.log(`foldingRange on ${last.name}: ${ranges.length} range(s)`);
        for (const r of ranges)
            console.log(`  lines ${r.startLine + 1}-${r.endLine + 1}` +
                (r.kind ? ` [${r.kind}]` : '') +
                ` -- ${JSON.stringify((lastLines[r.startLine] ?? '').trim())}`);
    }

    // The text under the reply's range is printed beside the prose: a doc
    // served on the wrong word is a passing request and a wrong answer, and
    // only the slice tells them apart.
    if (hoverCursor !== null) {
        const [line, character] = hoverCursor.split(':').map(Number);
        const hover = await request({
            method: 'textDocument/hover',
            params: { textDocument: { uri: last.uri }, position: { line, character } }
        }, 'the hover result');
        const result = hover.result ?? null;
        if (result === null) {
            console.log(`hover at ${hoverCursor} of ${last.name}: nothing`);
        } else {
            const contents = result.contents ?? {};
            const range = result.range;
            const word = range
                ? (await readFile(documentPaths.at(-1), 'utf8')).split('\n')[range.start.line]
                    .slice(range.start.character, range.end.character)
                : '';
            console.log(`hover at ${hoverCursor} of ${last.name}: `
                + `[${contents.kind ?? '?'}] ${JSON.stringify(contents.value ?? contents)}`
                + (range ? ` over "${word}"` : ''));
        }
    }

    await request({ method: 'shutdown' }, 'the shutdown result');
    send({ method: 'exit' });

    const code = await new Promise((resolve) => server.on('exit', resolve));
    console.log(`server exit code: ${code}`);
    process.exit(code === 0 ? 0 : 1);
} catch (error) {
    console.error(String(error.message ?? error));
    server.kill();
    process.exit(1);
}
