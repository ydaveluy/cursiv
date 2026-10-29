// The @CURSIV_NEW_ID@ extension for VS Code. It starts the language server
// `@CURSIV_NEW_ID@_lsp` and talks to it over standard input and output.

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

// How to start the server: a native server runs directly, and a WebAssembly
// server runs with the Node.js that runs this extension.
interface ServerCommand {
    command: string;
    args: string[];
    env?: Record<string, string>;
}

// Native and WebAssembly builds name the server the same way, so the first
// bytes of the file tell them apart.
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

// The server built by the active CMake preset: when you press F5,
// `launch.json` sets CURSIV_SERVER_DIR to its build folder.
function presetCommand(context: vscode.ExtensionContext): ServerCommand | undefined {
    const directory = process.env.CURSIV_SERVER_DIR?.trim();
    if (!directory) return undefined;
    const built = path.join(directory, executableName());
    if (!fs.existsSync(built)) return undefined;
    if (!isWasm(built)) return { command: built, args: [] };
    return wasmRun(built, path.join(context.extensionPath, 'server', 'wasi-run.mjs'));
}

// The WebAssembly server under `server/`, which a WASI build copies there. It
// runs on every platform, so a packaged extension prefers it.
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
        // A WebAssembly server cannot see how many CPU cores there are.
        env: { CURSIV_WORKERS: String(Math.max(0, os.availableParallelism() - 1)) },
    };
}

// The native server: the packaged copy under `bin/`, or the one CMake built
// in `../build/src/`.
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
    // Let AI assistants use the language server through MCP. They see the
    // same documents as the editor, including unsaved changes.
    context.subscriptions.push(startMcpBridge(ID, '@CURSIV_NEW_CLASS@', client, outputChannel));
}

export async function deactivate(): Promise<void> {
    const running = client;
    client = undefined;
    await running?.stop();
}
