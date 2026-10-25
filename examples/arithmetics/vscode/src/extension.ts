// The Arithmetics extension: it spawns `arithmetics_lsp` and speaks the protocol
// to it over stdio. Standalone -- no shared client -- so this folder packages on
// its own.

import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import * as vscode from 'vscode';
import {
    LanguageClient,
    TransportKind,
    type Executable,
    type LanguageClientOptions,
    type ServerOptions
} from 'vscode-languageclient/node';
import { startMcpBridge } from './mcp';

const KEY = 'arithmetics';
const LABEL = 'Arithmetics';
const SERVER = 'arithmetics_lsp';
const LANGUAGES = ["arithmetics"];

function executableName(): string {
    return process.platform === 'win32' ? `${SERVER}.exe` : SERVER;
}

function addCandidate(candidates: string[], value: string | undefined): void {
    const trimmed = value?.trim();
    if (trimmed) {
        candidates.push(trimmed);
    }
}

// Where the binary is, in the order the answers are trustworthy: what the user
// configured, what the environment says, then the default build tree. The
// extension lives at `examples/arithmetics/vscode`, so the repository root is three
// levels up and `build/examples/arithmetics/` is where CMake put the server.
// A native binary is its own command; a wasm module is a file Node runs, and
// the extension host is Node, so the wasm route needs no dependency.
interface ServerCommand {
    command: string;
    args: string[];
    env?: Record<string, string>;
}

// The wasm server, preferred when it is there: one artefact for every platform,
// and the sandbox comes with it -- the module reads only what the runner
// preopens. A packaged .vsix carries the optimized module and the runner under
// `server/`; a development tree serves them from `build-wasi/` and
// `tools/`. The module MUST be a threaded build: lsp-framework answers requests
// on its own thread pool, whose floor is one thread, so a threadless module
// fails every request with "thread constructor failed". Falls back to the
// native binary, which is what a build without the WASI toolchain produces.
function wasmCommand(module: string, runner: string): ServerCommand | undefined {
    if (!fs.existsSync(module) || !fs.existsSync(runner)) {
        return undefined;
    }
    return {
        command: process.execPath,
        args: ['--no-warnings', runner, module],
        // Wasi cannot count the host's cores: `hardware_concurrency()`
        // answers 1 there, so the pool would never be built. The host
        // knows, and says so. One fewer than the machine, as the
        // native default does.
        env: { CURSIV_WORKERS: String(Math.max(0, os.availableParallelism() - 1)) },
    };
}

function resolveWasmCommand(context: vscode.ExtensionContext): ServerCommand | undefined {
    const embedded = wasmCommand(
        path.join(context.extensionPath, 'server', `${SERVER}.wasm`),
        path.join(context.extensionPath, 'server', 'wasi-run.mjs'));
    if (embedded) {
        return embedded;
    }
    for (const root of repositoryRoots(context)) {
        const inTree = wasmCommand(
            path.join(root, 'build-wasi', 'examples', KEY, 'src', SERVER),
            path.join(root, 'tools', 'wasi-run.mjs'));
        if (inTree) {
            return inTree;
        }
    }
    return undefined;
}

function repositoryRoots(context: vscode.ExtensionContext): string[] {
    const roots = [path.resolve(context.extensionPath, '..', '..', '..')];
    for (const folder of vscode.workspace.workspaceFolders ?? []) {
        roots.push(folder.uri.fsPath, path.resolve(folder.uri.fsPath, '..', '..', '..'));
    }
    return roots;
}

// A wasm module opens with `\0asm` and a host binary does not. The cross build
// names its output exactly like the native one, so the file itself is the only
// thing that says which of the two runners it needs.
function isWasm(file: string): boolean {
    let handle: number | undefined;
    try {
        handle = fs.openSync(file, 'r');
        const head = Buffer.alloc(4);
        return fs.readSync(handle, head, 0, 4, 0) === 4 &&
            head.toString('latin1') === '\0asm';
    } catch {
        return false;
    } finally {
        if (handle !== undefined) fs.closeSync(handle);
    }
}

// What the active CMake preset just built: `launch.json` hands its binary
// directory over, so F5 runs the artefact the editor is configured to build
// rather than whatever a previous cross left under `server/`.
function presetCommand(context: vscode.ExtensionContext): ServerCommand | undefined {
    const directory = process.env.CURSIV_SERVER_DIR?.trim();
    if (!directory) return undefined;
    const built = path.join(directory, executableName());
    if (!fs.existsSync(built)) return undefined;
    if (!isWasm(built)) return { command: built, args: [] };
    for (const root of repositoryRoots(context)) {
        const run = wasmCommand(built, path.join(root, 'tools', 'wasi-run.mjs'));
        if (run) return run;
    }
    return wasmCommand(built,
        path.join(context.extensionPath, 'server', 'wasi-run.mjs'));
}

function resolveServerCommand(context: vscode.ExtensionContext): string | undefined {
    const executable = executableName();
    const candidates: string[] = [];

    addCandidate(candidates, vscode.workspace.getConfiguration().get<string>(`cursiv.${KEY}.serverPath`));
    addCandidate(candidates, process.env[`CURSIV_${KEY.replace(/-/g, '_').toUpperCase()}_SERVER`]);

    // The launch configuration opens `examples/arithmetics/example`, so the
    // repository root is three levels above the workspace folder too.
    const roots = repositoryRoots(context);
    for (const root of roots) {
        addCandidate(candidates, path.join(root, 'build', 'examples', KEY, 'src', executable));
    }

    for (const candidate of new Set(candidates)) {
        // A configured value with no separator in it is a command name, which is
        // how an installed server on PATH is named; anything else must exist on
        // disk before it is worth spawning.
        if (!candidate.includes(path.sep) && !candidate.includes('/')) {
            return candidate;
        }
        if (fs.existsSync(candidate)) {
            return path.resolve(candidate);
        }
    }
    return undefined;
}

let client: LanguageClient | undefined;

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    // The active preset first, then the packaged wasm; the setting overrides
    // both, since a configured `serverPath` names a binary the author chose.
    const configured = vscode.workspace.getConfiguration().get<string>(`cursiv.${KEY}.serverPath`)?.trim();
    const found = configured
        ? undefined
        : presetCommand(context) ?? resolveWasmCommand(context);
    const command = found ? found.command : resolveServerCommand(context);
    if (!command) {
        void vscode.window.showErrorMessage(
            `Cannot find ${executableName()}. Build it with ` +
            `\`cmake --build build --target ${SERVER}\`, or set \`cursiv.${KEY}.serverPath\`.`
        );
        return;
    }

    const outputChannel = vscode.window.createOutputChannel(`cursiv ${LABEL}`);
    context.subscriptions.push(outputChannel);
    outputChannel.appendLine(found && found.args.length > 0
        ? `server: wasm ${found.args[found.args.length - 1]}`
        : `server: ${command}`);

    // One entry for both modes: a native server has no --inspect to add, so the
    // debug configuration is the run configuration.
    const executable: Executable = {
        command,
        args: found ? found.args : [],
        transport: TransportKind.stdio,
        ...(found?.env ? { options: { env: { ...process.env, ...found.env } } } : {}),
    };
    const serverOptions: ServerOptions = { run: executable, debug: executable };
    const clientOptions: LanguageClientOptions = {
        documentSelector: LANGUAGES.map((language) => ({ scheme: 'file', language })),
        outputChannel
    };

    client = new LanguageClient(`cursiv-${KEY}`, `cursiv ${LABEL}`, serverOptions, clientOptions);
    await client.start();
    // The MCP door onto the same session: a model connected here reads what
    // the editor holds unsaved.
    context.subscriptions.push(startMcpBridge(KEY, LABEL, client, outputChannel));
}

export async function deactivate(): Promise<void> {
    const running = client;
    client = undefined;
    await running?.stop();
}
