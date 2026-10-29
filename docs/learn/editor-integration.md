# Ship an editor extension

The scaffold includes a VS Code client. It needs VS Code 1.86 or later with a
native server, and 1.101 or later with a WebAssembly (WASI) server. To build
the client, install Node.js with npm, and the recommended CMake Tools and
clangd extensions.

## Press F5

Open the project folder in VS Code and press **F5**. The task builds `<id>_lsp`
in the build directory selected by CMake Tools and bundles the client. A
second window opens on `example/` with the extension loaded.

Open `example/hello.<ext>` there. Try completion and go-to-definition, then
type an unknown reference to see a diagnostic. The language's output channel
shows which server runs.

GCC and Clang presets run a native server. WASI presets run a WebAssembly
module through Node. The supplied presets need Ninja.

## Distribute with WebAssembly

One WebAssembly server works on every platform, where a native server needs
one build per platform. Follow [Cross-compiling to wasm](../recipes/wasm.md)
to build it into `vscode/server/` before packaging.

## Selecting the server

The client runs the first server it finds:

1. the `<id>.server.path` setting, a native executable;
2. the server built by the active preset, which is what F5 runs;
3. `server/<id>_lsp.wasm` in the extension, as a packaged `.vsix` carries;
4. a native `bin/<id>_lsp` in the extension, then `../build/src/<id>_lsp`.

Before VS Code 1.101, select a GCC or Clang preset or set
`<id>.server.path`. On Windows, native executables end in `.exe`.

## Declaring the language to the editor

Update these files when you change the language:

| File | Configures |
|---|---|
| `vscode/package.json` | Language id, file extensions and syntax file |
| `vscode/syntaxes/<id>.tmLanguage.json` | TextMate rules for initial syntax highlighting |
| `vscode/language-configuration.json` | Brackets, comment toggling and auto-closing pairs |

Cursiv does not update the TextMate rules: add new grammar keywords to them
yourself. Semantic tokens refine the highlighting once the server has parsed
the document.

## What you get without writing a provider

Completion, hover, go-to-definition, find-references, rename, document and
workspace symbols, folding, selection ranges, semantic tokens and code actions
work by default. Signature help and inlay hints need providers of your own.

To customize a feature, implement a provider and install it in the LSP
module. For example, after defining `MyLangHoverProvider`:

```cpp title="src/mylang/lsp/LspModule.cpp"
void install_lsp_module(MyLangServices& services) {
  cursiv::install_default_lsp_services(services);
  services.hoverProvider = std::make_unique<MyLangHoverProvider>(services);
}
```

See [Services](../reference/services.md).

## Packaging

Build the server you want to ship, then package the client:

```bash
cd vscode
npm install
npm run package
```

The `.vsix` must carry the server: the WebAssembly module, runner and license
notices under `server/`, or the native executable under `bin/`. A build
directory outside `vscode/` is not packaged. Check the contents with
`npx vsce ls --no-dependencies`.

For a WebAssembly-only extension, set `engines.vscode` to `^1.101.0` in
`vscode/package.json`. The scaffold's `^1.86.0` allows native servers.

## MCP integration

The client also connects Model Context Protocol (MCP) clients to the
language server, including unsaved buffers. VS Code 1.101 or later registers
it automatically. On older versions, connect an MCP client with the URL and
token shown in the language's output channel. See
[MCP server](../reference/mcp.md).
