# Integrate with VS Code

A Pegium language server starts with one function call; the editor client is thin glue. This page walks through the shipped **VS Code** client (under `examples/<language>/vscode/` plus a `package.json`) — use it as a template. The server itself is a standard language server, so any LSP-capable editor connects the same way (see [Adapting it to your language](#adapting-it-to-your-language)).

## The server side

Your `lsp/main.cpp` is a single call:

```cpp
#include <pegium/lsp/runtime/LanguageServerRuntime.hpp>

int main(int argc, char **argv) {
  return pegium::runLanguageServerMain<statemachine::StatemachineSharedServices>(
      argc, argv, "statemachine-lsp",
      statemachine::registerStatemachineLspServices);
}
```

The resulting binary speaks LSP. The example clients run it as a TCP server with `--port=<port>` and connect over a localhost socket.

## The VS Code client

Two pieces, both in `examples/statemachine/`.

`package.json` declares the language and where to find the server:

```json
"contributes": {
  "languages": [
    { "id": "statemachine", "extensions": [".statemachine"],
      "configuration": "./vscode/language-configuration.json" }
  ],
  "configuration": {
    "properties": {
      "pegium.statemachine.serverPath": {
        "type": "string",
        "description": "Path to the pegium-example-statemachine-lsp executable."
      }
    }
  }
}
```

`vscode/src/extension.ts` locates the server, launches it, and starts a client:

```ts
import { LanguageClient } from 'vscode-languageclient/node';

export async function activate(context: vscode.ExtensionContext) {
  const serverCommand = resolveServerCommand(context); // setting / env / build dir
  const serverOptions = async () => {
    const port = await allocatePort();
    const child = childProcess.spawn(serverCommand, [`--port=${port}`]);
    return waitForServerSocket(port, child); // { reader: socket, writer: socket }
  };
  const clientOptions = {
    documentSelector: [{ scheme: 'file', language: 'statemachine' }],
  };
  const client = new LanguageClient(
      'pegium-statemachine', 'Pegium Statemachine', serverOptions, clientOptions);
  await client.start();
}
```

The example resolves the server path in order from:

- the `pegium.<lang>.serverPath` setting,
- a `PEGIUM_<LANG>_SERVER` environment variable,
- common `build/examples/<language>/` locations.

It then connects over the socket. The full working version is in [`examples/statemachine/vscode/src/extension.ts`](https://github.com/ydaveluy/pegium/tree/main/examples/statemachine/vscode/src/extension.ts).

## Adapting it to your language

1. Copy `examples/<closest>/vscode/` and its `package.json`.
2. Replace the language `id`, the file `extensions`, and the `serverPath` setting key.
3. Point the server-path resolution at your own `build/.../<your>-lsp` binary.

Any LSP-capable editor works the same way. A Pegium server is a standard language server, so only the client glue differs.

## Related pages

- [LSP Services](lsp-services.md)
- [Build a Language End-to-End](../learn/walkthrough.md)
