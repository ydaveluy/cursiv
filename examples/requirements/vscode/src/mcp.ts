// The MCP server of the extension. It accepts HTTP requests from this machine
// only, with a token created for each session, and forwards them to the
// language server.

import { isUtf8 } from 'node:buffer';
import * as crypto from 'node:crypto';
import * as http from 'node:http';
import * as vscode from 'vscode';
import type { LanguageClient } from 'vscode-languageclient/node';

const MCP_METHOD = 'cursiv/mcp';

// The MCP registration API of VS Code 1.101 and later, declared here so the
// extension still builds and runs on VS Code 1.91.
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

type Fields = Record<string, unknown>;
type Tool = { name: string; inputSchema: Fields };
type Reply = { result?: { tools?: Tool[] }; error?: { code: number } } | null;

function fields(value: unknown): Fields {
    return value !== null && typeof value === 'object' && !Array.isArray(value) ? value as Fields : {};
}

function headerMatches(header: string | string[] | undefined, value: unknown): boolean {
    if (typeof header !== 'string') return false;
    if (header.startsWith('=?base64?') && header.endsWith('?=')) {
        const encoded = header.slice(9, -2);
        const bytes = Buffer.from(encoded, 'base64');
        if (bytes.toString('base64') !== encoded || !isUtf8(bytes)) return false;
        header = bytes.toString('utf8');
    } else if (/[^\x20-\x7e]/.test(header) || header.trim() !== header) {
        return false;
    }
    if (typeof value === 'number') {
        return Number.isSafeInteger(value) && /^-?\d+(?:\.0+)?$/.test(header) && Number(header) === value;
    }
    return header === String(value);
}

function parametersMatch(schema: Fields | undefined, args: unknown, headers: http.IncomingHttpHeaders): boolean {
    const pending: [Fields | undefined, unknown][] = [[schema, args]];
    while (pending.length) {
        const [parent, object] = pending.pop()!;
        for (const [key, property] of Object.entries(fields(parent?.properties))) {
            const child = fields(property);
            const value = Object.hasOwn(fields(object), key) ? fields(object)[key] : undefined;
            const name = child['x-mcp-header'];
            if (typeof name === 'string') {
                const header = headers[`mcp-param-${name.toLowerCase()}`];
                if (value == null ? header !== undefined : !headerMatches(header, value)) return false;
            }
            if (child.properties) pending.push([child, value]);
        }
    }
    return true;
}

export function startMcpBridge(
    key: string,
    label: string,
    client: LanguageClient,
    outputChannel: vscode.OutputChannel,
): vscode.Disposable {
    const token = crypto.randomUUID();
    let schemas: Map<string, Fields> | undefined;
    // The requests the language server has yet to answer, all answered at
    // once when the bridge closes, as are any later ones.
    let waiting: Set<() => void> | undefined = new Set();

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
            answer(status, { jsonrpc: '2.0', error: { code, message } });
        // Refuse requests from web pages (protection against DNS rebinding).
        if (!isLocal(request.headers.host) || !isLocal(request.headers.origin)) {
            return refusal(403, -32000, 'forbidden origin');
        }
        if (request.headers.authorization !== `Bearer ${token}`) {
            return refusal(401, -32000, 'missing or wrong bearer token');
        }
        if (request.url !== '/mcp') return answer(404);
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
        request.on('end', async () => {
            let message: unknown;
            try {
                message = JSON.parse(body);
            } catch {
                return refusal(400, -32700, 'parse error');
            }
            const rpc = fields(message);
            const id = typeof rpc.id === 'string' || Number.isSafeInteger(rpc.id) ? rpc.id : undefined;
            const params = fields(rpc.params);
            const meta = fields(params._meta);
            const reject = (status: number, code: number, message: string): void =>
                answer(status, { jsonrpc: '2.0', id, error: { code, message } });
            const header = request.headers['mcp-protocol-version'];
            const version = meta['io.modelcontextprotocol/protocolVersion'];
            const modern = version !== undefined || 'io.modelcontextprotocol/clientCapabilities' in meta ||
                rpc.method === 'server/discover' || header === '2026-07-28' ||
                (header === undefined && rpc.method !== 'initialize' && request.headers['mcp-method'] !== undefined);
            if (!Array.isArray(message) && (rpc.jsonrpc !== '2.0' || ('id' in rpc && id === undefined))) {
                return reject(400, -32600, 'invalid JSON-RPC message');
            }
            if (modern) {
                if (typeof rpc.method !== 'string') return reject(400, -32600, 'expected a single MCP request');
                if (id !== undefined && typeof version !== 'string' && header) {
                    return reject(400, -32602, 'request metadata must include protocolVersion');
                }
                const nameKey = rpc.method === 'resources/read' ? 'uri'
                    : rpc.method === 'tools/call' || rpc.method === 'prompts/get' ? 'name' : undefined;
                if (id !== undefined && (typeof version !== 'string' || header !== version ||
                    request.headers['mcp-method'] !== rpc.method || (nameKey &&
                    (typeof params[nameKey] !== 'string' || !headerMatches(request.headers['mcp-name'], params[nameKey]))))) {
                    return reject(400, -32020, 'MCP headers must match the request');
                }
            }
            const cancel = new vscode.CancellationTokenSource();
            const stopped = (): void => {
                cancel.cancel();
                reject(503, -32603, 'the language server stopped');
            };
            const pending = waiting;
            if (!pending) {
                stopped();
                cancel.dispose();
                return;
            }
            pending.add(stopped);
            const closed = (): void => {
                pending.delete(stopped);
                cancel.cancel();
            };
            response.once('close', closed);
            const remember = (reply: Reply): void => {
                if (reply?.result?.tools) schemas = new Map(reply.result.tools.map(t => [t.name, t.inputSchema]));
            };
            try {
                if (modern && rpc.method === 'tools/call') {
                    if (!schemas) {
                        const catalog = await client.sendRequest<Reply>(MCP_METHOD, {
                            jsonrpc: '2.0', id: crypto.randomUUID(), method: 'tools/list', params: { _meta: meta },
                        }, cancel.token);
                        remember(catalog);
                        if (catalog?.error) {
                            if (pending.has(stopped)) answer(400, { ...catalog, id });
                            return;
                        }
                    }
                    if (!parametersMatch(schemas?.get(String(params.name)), params.arguments, request.headers)) {
                        if (pending.has(stopped)) reject(400, -32020, 'MCP parameter headers must match the arguments');
                        return;
                    }
                }
                if (!pending.has(stopped)) return;
                const payload = modern ? message : { message, protocolVersion: header ?? '2025-03-26' };
                const reply = await client.sendRequest<Reply>(MCP_METHOD, payload, cancel.token);
                if (!pending.has(stopped)) return;
                if (modern && rpc.method === 'tools/list') remember(reply);
                if (reply === null) return answer(202);
                const code = reply.error?.code;
                const status = code === -32022 ? 400 : !modern || code === undefined ? 200
                    : code === -32601 ? 404 : code === -32603 ? 500 : 400;
                answer(status, reply);
            } catch (error) {
                if (pending.has(stopped)) reject(500, -32603, String((error as Error)?.message ?? error));
            } finally {
                pending.delete(stopped);
                response.off('close', closed);
                cancel.dispose();
            }
        });
    });

    const registration = new Promise<vscode.Disposable | undefined>((resolve) => {
        // If the port cannot be opened, log the error and run without the MCP
        // server. Later server errors are logged too.
        server.on('error', (error: Error) => {
            outputChannel.appendLine(`mcp: ${error.message}`);
            resolve(undefined);
        });
        server.listen(0, '127.0.0.1', () => {
            const address = server.address();
            if (address === null || typeof address === 'string') {
                return resolve(undefined);
            }
            const url = `http://127.0.0.1:${address.port}/mcp`;
            // Other MCP clients connect with this URL and token, for example:
            //   claude mcp add --transport http <name> <url> --header "Authorization: Bearer <token>"
            outputChannel.appendLine(`mcp: ${url} (Authorization: Bearer ${token})`);
            const api = vscode as unknown as McpRegistry;
            const register = api.lm?.registerMcpServerDefinitionProvider;
            const HttpDefinition = api.McpHttpServerDefinition;
            // Before 1.101, VS Code cannot register MCP servers: clients use
            // the URL above.
            if (register === undefined || HttpDefinition === undefined) {
                return resolve(undefined);
            }
            try {
                resolve(register.call(api.lm, `cursiv.${key}.mcp`, {
                    provideMcpServerDefinitions: () => [
                        new HttpDefinition(`cursiv ${label}`, vscode.Uri.parse(url), {
                            Authorization: `Bearer ${token}`,
                        }),
                    ],
                }));
            } catch (error) {
                outputChannel.appendLine(`mcp: ${String((error as Error)?.message ?? error)}`);
                resolve(undefined);
            }
        });
    });

    return new vscode.Disposable(() => {
        void registration.then((r) => r?.dispose());
        server.close();
        const pending = waiting;
        waiting = undefined;
        pending?.forEach((stopped) => stopped());
        pending?.clear();
    });
}
