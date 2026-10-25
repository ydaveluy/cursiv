// The MCP bridge: the endpoint lives in the language server and rides the LSP
// wire as `cursiv/mcp`; this hosts the standard Streamable HTTP transport the
// module itself cannot open under WASI. Localhost only, one bearer token per
// session, no SSE -- the endpoint sends nothing on its own.

import * as crypto from 'node:crypto';
import * as http from 'node:http';
import * as vscode from 'vscode';
import type { LanguageClient } from 'vscode-languageclient/node';

const MCP_METHOD = 'cursiv/mcp';

// Announcing the server to the editor is VS Code 1.101 API; serving it is not.
// Declared here rather than taken from `@types/vscode` so the extension still
// compiles, installs and serves against the older engine it asks for.
interface McpRegistry {
    lm?: {
        registerMcpServerDefinitionProvider?(
            id: string,
            provider: { provideMcpServerDefinitions: () => unknown[] },
        ): vscode.Disposable;
    };
    McpHttpServerDefinition?: new (
        label: string,
        uri: vscode.Uri,
        headers?: Record<string, string>,
    ) => unknown;
}

function isLocal(value: string | undefined): boolean {
    if (!value) {
        return true;
    }
    try {
        const host = new URL(value.includes('://') ? value : `http://${value}`).hostname;
        return host === '127.0.0.1' || host === 'localhost' || host === '[::1]';
    } catch {
        return false;
    }
}

export function startMcpBridge(
    key: string,
    label: string,
    client: LanguageClient,
    outputChannel: vscode.OutputChannel,
): vscode.Disposable {
    const token = crypto.randomUUID();

    const server = http.createServer((request, response) => {
        const answer = (status: number, body?: unknown): void => {
            if (body === undefined) {
                response.writeHead(status).end();
                return;
            }
            response
                .writeHead(status, { 'Content-Type': 'application/json' })
                .end(JSON.stringify(body));
        };
        const refusal = (status: number, code: number, message: string): void =>
            answer(status, { jsonrpc: '2.0', id: null, error: { code, message } });
        // Anti DNS-rebinding: a browser page reaching for the port names its
        // origin, and no browser origin has business here.
        if (!isLocal(request.headers.host) || !isLocal(request.headers.origin)) {
            return refusal(403, -32000, 'forbidden origin');
        }
        if (request.headers.authorization !== `Bearer ${token}`) {
            return refusal(401, -32000, 'missing or wrong bearer token');
        }
        if (request.method !== 'POST') {
            return answer(405);
        }
        let body = '';
        request.setEncoding('utf8');
        request.on('data', (chunk: string) => {
            body += chunk;
            if (body.length > 16 * 1024 * 1024) {
                request.destroy();
            }
        });
        request.on('end', () => {
            let message: unknown;
            try {
                message = JSON.parse(body);
            } catch {
                return refusal(400, -32700, 'parse error');
            }
            const id = (message as { id?: unknown })?.id ?? null;
            client.sendRequest(MCP_METHOD, message).then(
                // A notification answers nothing, which the transport spells 202.
                (result) => (result === null ? answer(202) : answer(200, result)),
                (error) => answer(500, {
                    jsonrpc: '2.0',
                    id,
                    error: { code: -32603, message: String((error as Error)?.message ?? error) },
                }),
            );
        });
    });

    const registration = new Promise<vscode.Disposable | undefined>((resolve) => {
        server.listen(0, '127.0.0.1', () => {
            const address = server.address();
            if (address === null || typeof address === 'string') {
                return resolve(undefined);
            }
            const url = `http://127.0.0.1:${address.port}/mcp`;
            // The line an external client copies:
            //   claude mcp add --transport http <name> <url> --header "Authorization: Bearer <token>"
            outputChannel.appendLine(`mcp: ${url} (Authorization: Bearer ${token})`);
            const api = vscode as unknown as McpRegistry;
            const register = api.lm?.registerMcpServerDefinitionProvider;
            const HttpDefinition = api.McpHttpServerDefinition;
            // Before 1.101 the editor has no MCP registry, so the line above is
            // the whole wiring: the server serves, a client is pointed at it.
            if (register === undefined || HttpDefinition === undefined) {
                return resolve(undefined);
            }
            resolve(register.call(api.lm, `cursiv.${key}.mcp`, {
                provideMcpServerDefinitions: () => [
                    new HttpDefinition(`cursiv ${label}`, vscode.Uri.parse(url), {
                        Authorization: `Bearer ${token}`,
                    }),
                ],
            }));
        });
    });

    return new vscode.Disposable(() => {
        void registration.then((r) => r?.dispose());
        server.close();
    });
}
