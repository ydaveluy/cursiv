# Cross-compiling to WebAssembly

A WebAssembly language server is one module that runs on every platform.
The scaffold's VS Code client runs it with Node.js; this requires VS Code
1.101 or later. The steps below use the `wasi-release` preset of a generated
project.

## Install the tools

By default, the presets use wasi-sdk 34 or newer, which provides a WASI compiler
and libraries. A generated project's script installs and verifies it, the same
way on Linux, macOS and Windows, on your machine and in CI:

```bash
cmake -P tools/fetch-wasi-sdk.cmake
```

The presets find it in `~/wasi-sdk-34.0`, or `/opt/wasi-sdk`. To use an SDK
elsewhere, point `WASI_SDK` at it, or pass `-DWASI_SDK=/path/to/wasi-sdk` to
CMake:

```bash
export WASI_SDK=/path/to/wasi-sdk
```

To change the SDK version, replace `tools/fetch-wasi-sdk.cmake` and
`cmake/wasi.cmake` with those of the Cursiv version you use.

For Release builds, also put `wasm-opt` on your `PATH` to optimize the module.
Take it from a [Binaryen release](https://github.com/WebAssembly/binaryen/releases)
that supports the WebAssembly features of your wasi-sdk, including exception
handling. Without it, the build skips this step.

## Build the server

```bash
cmake --preset wasi-release
cmake --build --preset wasi-release --target mylang_lsp
```

The parser generator is still built and run natively on your machine. The
WASI presets use their own build directories, apart from the native ones.

LTO is disabled for WASI, including explicit CMake IPO requests, because the
current SDK does not preserve C++ exception handlers with LTO. Native builds
keep their own IPO setting.

## Use your own toolchain

The SDK is only the default. To use another compiler, write a toolchain file
that names it, adds the flags it needs for C++ exceptions, and includes the
settings every WASI toolchain shares:

```cmake
# cmake/my-wasi.cmake
set(CMAKE_C_COMPILER   /path/to/cc)
set(CMAKE_CXX_COMPILER /path/to/c++)
set(CMAKE_SYSROOT      /path/to/wasi-sysroot)
include(${CMAKE_CURRENT_LIST_DIR}/wasi-platform.cmake)
```

Then pass it to a preset:

```bash
cmake --preset wasi-release --toolchain cmake/my-wasi.cmake
```

Cursiv needs C++ exceptions and wasi-libc. It adds the tail-call options of the
parser for Clang only; another compiler must enable tail calls itself.

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

## Add application imports

The Cursiv runner can load an application-owned host module before starting
WebAssembly:

```bash
node tools/wasi-run.mjs --host-module /path/to/host.mjs server.wasm --stdio
```

The module exports `createHost(context)`, which may be asynchronous and returns
`{ imports, env, instantiated }`, each optional. For example:

```js
export function createHost({ memory }) {
  return {
    imports: {
      app: {
        read_byte(offset) {
          return new Uint8Array(memory().buffer)[offset >>> 0];
        },
      },
    },
    env: { APP_MODE: 'embedded' },
  };
}
```

Call `memory()` inside imported functions to obtain the current memory after
any growth. Read an address as `offset >>> 0`: it arrives signed, negative past
2 GiB. The context also contains the WebAssembly `args` and the resolved
`runner` and `hostModule` paths. Application environment entries supplement the
host environment; `PWD` always reflects the actual working directory.

`instantiated(instance)` runs on each thread after the module's start function
and before `_start` or the thread's entry point. A host can use it to load more
code into that instance.

`CURSIV_WASI_HOST_MODULE` selects the same module without a command-line flag.
An explicit flag takes precedence. The selection reaches the command and its
nested WASI threads, and is exposed in their environment for application hosts
that launch further modules. Options after `server.wasm` remain application
arguments. The application owns its imports; Cursiv supplies WASI, memory and
thread lifecycle support.

## Advanced: build configuration

To start again from a fresh CMake cache, run
`cmake --fresh --preset wasi-release`. The
[CMake reference](../reference/cmake.md#options) lists the WASI options, such
as the memory limit.
