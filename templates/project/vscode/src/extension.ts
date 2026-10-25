// The @CURSIV_NEW_ID@ extension: it spawns `@CURSIV_NEW_ID@_lsp` and speaks
// the protocol to it over stdio.

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

const ID = '@CURSIV_NEW_ID@';
const SERVER = '@CURSIV_NEW_ID@_lsp';

function executableName(): string {
    return process.platform === 'win32' ? `${SERVER}.exe` : SERVER;
}

// A native binary is its own command; a wasm module is a file Node runs, and
// the extension host IS Node, so the wasm route needs no dependency.
interface ServerCommand {
    command: string;
    args: string[];
    env?: Record<string, string>;
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
    return wasmRun(built, path.join(context.extensionPath, 'server', 'wasi-run.mjs'));
}

// The wasm server, preferred when it is there: one artefact for every platform,
// and the sandbox comes with it -- the module reads only what the runner
// preopens. `cursiv_package_language` refreshes both files under `server/` on a
// threaded cross build, and a packaged .vsix carries them.
function wasmCommand(context: vscode.ExtensionContext): ServerCommand | undefined {
    return wasmRun(path.join(context.extensionPath, 'server', `${SERVER}.wasm`),
                   path.join(context.extensionPath, 'server', 'wasi-run.mjs'));
}

function wasmRun(module: string, runner: string): ServerCommand | undefined {
    if (!fs.existsSync(module) || !fs.existsSync(runner)) {
        return undefined;
    }
    return {
        command: process.execPath,
        args: ['--no-warnings', runner, module],
        // WASI cannot count the host's cores: `hardware_concurrency()` answers
        // one there, so the pool would never be built. The host knows.
        env: { CURSIV_WORKERS: String(Math.max(0, os.availableParallelism() - 1)) },
    };
}

// The extension lives at `<project>/vscode`, so `../build/src/` is where
// CMake put the native server.
function nativeCommand(context: vscode.ExtensionContext): ServerCommand | undefined {
    const name = executableName();
    for (const candidate of [
        path.join(context.extensionPath, 'bin', name),
        path.join(context.extensionPath, '..', 'build', 'src', name)
    ]) {
        if (fs.existsSync(candidate)) {
            return { command: path.resolve(candidate), args: [] };
        }
    }
    return undefined;
}

let client: LanguageClient | undefined;

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    const configured = vscode.workspace.getConfiguration()
        .get<string>(`${ID}.server.path`)?.trim();
    const server: ServerCommand | undefined = configured
        ? { command: configured, args: [] }
        : presetCommand(context) ?? wasmCommand(context) ?? nativeCommand(context);
    if (!server) {
        void vscode.window.showErrorMessage(
            `Cannot find ${SERVER}. Build it with ` +
            `\`cmake --build --preset wasi-release --target ${SERVER}\`, or set ` +
            `\`${ID}.server.path\`.`
        );
        return;
    }

    const outputChannel = vscode.window.createOutputChannel(ID);
    context.subscriptions.push(outputChannel);
    outputChannel.appendLine(`server: ${server.command} ${server.args.join(' ')}`);

    // A native server has no --inspect to add, so the debug configuration is
    // the run configuration.
    const executable: Executable = {
        command: server.command,
        args: server.args,
        transport: TransportKind.stdio,
        ...(server.env ? { options: { env: { ...process.env, ...server.env } } } : {})
    };
    const serverOptions: ServerOptions = { run: executable, debug: executable };
    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: ID }],
        outputChannel
    };

    client = new LanguageClient(ID, '@CURSIV_NEW_CLASS@', serverOptions, clientOptions);
    await client.start();
    // The MCP door onto the same session: a model connected here reads what
    // the editor holds unsaved.
    context.subscriptions.push(startMcpBridge(ID, '@CURSIV_NEW_CLASS@', client, outputChannel));
}

export async function deactivate(): Promise<void> {
    const running = client;
    client = undefined;
    await running?.stop();
}
