# Cross-compiling to WebAssembly

The scaffold's VS Code client can run a WebAssembly language server through
Node. This requires VS Code 1.101 or later, whose extension host includes
Node.js 22. It lets you distribute one server module for the supported platforms.

Start from a generated language project. The steps below use its
`wasi-release` preset.

## Install the tools

Install wasi-sdk 34 or newer. It provides the C++ compiler and libraries for
WASI, the system interface used by the WebAssembly runner.

Set the SDK directory:

```bash
export WASI_SDK=$HOME/wasi-sdk-34.0
```

You can also pass `-DWASI_SDK=/path/to/wasi-sdk` when configuring CMake.

For Release builds, install [Binaryen 120 or later](https://github.com/WebAssembly/binaryen/releases)
and make `wasm-opt` available on `PATH` before building. SDK Clang invokes it
automatically after linking at the selected optimization level (`-O3` for
Release). Without `wasm-opt` on `PATH`, Clang skips the pass.

## Build the server

```bash
cmake --preset wasi-release
cmake --build --preset wasi-release --target mylang_lsp
```

The supplied WASI presets enable `CURSIV_WASI_THREADS`, which the language
server requires. CMake builds and runs the parser generator on your host
machine automatically.

Select `wasi-release` or `wasi-debug` in VS Code and press F5 to try the
WebAssembly server. See [Editor integration](../learn/editor-integration.md)
for the client setup.

## Package the extension

Building the server copies the module, `wasi-run.mjs` and the Cursiv,
lsp-framework and mimalloc license notices into `vscode/server/`.
Include that directory in the extension's `package.json` file list.

After building the server:

```bash
cd vscode
npm install
npm run package
```

The resulting `.vsix` contains the client and the WebAssembly server.
Check the package contents as described in
[Editor integration](../learn/editor-integration.md#packaging).

## Advanced: build configuration

Use separate native and WASI build directories. The `--target mylang_lsp`
command above builds the server and its dependencies.

The threaded toolchain allows shared memory to grow up to 2 GiB; this is an
addressing limit, not an upfront allocation. Set `CURSIV_WASI_MAX_MEMORY` when
configuring to choose another limit. Existing caches retain their saved value.

To skip the Binaryen optimization pass for an executable during development:

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  target_link_options(mylang_cli PRIVATE --no-wasm-opt)
endif()
```

To configure with a fresh CMake cache, run
`cmake --fresh --preset wasi-release`. CMake then redetects the compiler and
tool paths. The [CMake reference](../reference/cmake.md#options) lists the
related options.
