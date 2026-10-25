# Ship an editor extension

The scaffold includes a VS Code client. Native servers support VS Code 1.86
or later. WASI servers require VS Code 1.101 or later: the runner uses the
editor's embedded Node.js 22 or later.
See the [VS Code 1.101 runtime update](https://code.visualstudio.com/updates/v1_101#_electron-35-update).

Install Node.js with npm and the recommended CMake Tools and clangd extensions
to build the client.
After configuring a preset, CMake Tools copies its `compile_commands.json`
to the project root for clangd's completion, navigation and diagnostics.
Clangd formats the C++ sources using the project's `.clang-format`.
The client's launch task uses the build directory selected by CMake Tools.

## Press F5

Open the project folder in VS Code and press **F5**. The task builds
`<id>_lsp` in your selected CMake build directory and bundles the client.
A second window opens on `example/` with the extension loaded.

Open `example/hello.<ext>` in the second window. Check completion and
go-to-definition, then enter an unknown reference to see a diagnostic. The
language's output channel shows which server was launched.

The GCC and Clang presets run a native server. WASI presets run a WebAssembly
module through Node. Ninja is required when using the supplied presets.

## Distribute with WebAssembly

For distribution, you can package one WebAssembly server for all supported
hosts instead of building a native executable for each platform. Follow
[Cross-compiling to wasm](../recipes/wasm.md) to build the threaded module and
populate `vscode/server/` before packaging.

## Selecting the server

The client looks for the server in this order:

1. `<id>.server.path` — an explicit native executable path.
2. `CURSIV_SERVER_DIR` — the active preset's binary directory, passed by
   `launch.json`. This is the F5 path.
3. `<extension>/server/<id>_lsp.wasm` — what a packaged `.vsix` carries.
4. `<extension>/bin/<id>_lsp`, then `../build/src/<id>_lsp` — native fallbacks.

On older VS Code versions, select a GCC/Clang preset or set `<id>.server.path`
to use a native server. On Windows, native executable names end in `.exe`.

## Declaring the language to the editor

Update these files when you change the language:

| File | Configures |
|---|---|
| `vscode/package.json` | Language id, file extensions and syntax file |
| `vscode/syntaxes/<id>.tmLanguage.json` | TextMate rules for initial syntax highlighting |
| `vscode/language-configuration.json` | Brackets, comment toggling and auto-closing pairs |

Cursiv does not regenerate the TextMate rules. Update their keyword list when
you add grammar keywords; semantic tokens provide further highlighting once
the server has parsed the document.

## What you get without writing a provider

Completion, hover, go-to-definition, find-references, rename, document and
workspace symbols, folding, selection ranges, semantic tokens and code actions
are provided by default. Language-specific features such as signature help
and inlay hints require additional providers.

To customize a feature, implement a provider and install it in the LSP module.
For example, after defining `MyLangHoverProvider`:

```cpp title="src/mylang/lsp/LspModule.cpp"
void install_lsp_module(MyLangServices& services) {
  cursiv::install_default_lsp_services(services);
  services.hoverProvider = std::make_unique<MyLangHoverProvider>(services);
}
```

See [Services](../reference/services.md) for the setup of custom providers.

## Packaging

Build the server you want to distribute first, then package the client:

```bash
cd vscode
npm install
npm run package
```

For a WASI-only distribution, set `engines.vscode` to `^1.101.0` in
`vscode/package.json`. The scaffold's `^1.86.0` minimum supports native servers.

The package script bundles the TypeScript client before producing the `.vsix`.
Check its contents with `npx vsce ls --no-dependencies`: a wasm distribution
needs the module, runner and license notices under `server/`, while a native
distribution needs the executable under `bin/`. A build directory outside
`vscode/` is not part of the package.

## MCP integration

The client also provides a Model Context Protocol (MCP) bridge to the language
server, including unsaved editor buffers. It uses the existing LSP connection.
Automatic registration in VS Code requires 1.101 or later. On earlier versions,
connect an external MCP client using the URL and token shown in the language's
output channel. See [MCP server](../reference/mcp.md).
