# Cross-compiling to WebAssembly

A WebAssembly language server is one module for every platform. The
scaffold's VS Code client runs it through Node.js, which needs VS Code 1.101
or later. The steps below use the `wasi-release` preset of a generated
project.

## Install the tools

Install wasi-sdk 34 or newer, which provides the WASI compiler and libraries,
and point `WASI_SDK` at it:

```bash
export WASI_SDK=$HOME/wasi-sdk-34.0
```

You can also pass `-DWASI_SDK=/path/to/wasi-sdk` to CMake.

For Release builds, also put `wasm-opt` from
[Binaryen 120 or later](https://github.com/WebAssembly/binaryen/releases) on
your `PATH`. Without it, the build skips that optimization.

## Build the server

```bash
cmake --preset wasi-release
cmake --build --preset wasi-release --target mylang_lsp
```

CMake still builds and runs the parser generator on your machine. The WASI
presets use their own build directories, apart from the native ones.

To try the server, select `wasi-release` or `wasi-debug` in VS Code and press
F5. See [Ship an editor extension](../learn/editor-integration.md).

## Package the extension

The build copies the module, its runner `wasi-run.mjs` and the license
notices into `vscode/server/`. The scaffold's `vscode/package.json` already
includes that directory. Then package the client:

```bash
cd vscode
npm install
npm run package
```

The `.vsix` contains the client and the WebAssembly server. To check its
contents, see [Packaging](../learn/editor-integration.md#packaging).

## Advanced: build configuration

To start again from a fresh CMake cache, run
`cmake --fresh --preset wasi-release`. The
[CMake reference](../reference/cmake.md#options) lists the WASI options, such
as the memory limit.
