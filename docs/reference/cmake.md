# CMake API

The [scaffolder](../learn/scaffold.md) creates a configured project. Use this
page to add Cursiv to an existing project or change its build settings.

## Add Cursiv to a project

Add Cursiv with FetchContent, then declare your language target:

```cmake title="CMakeLists.txt"
include(FetchContent)
if(CURSIV_SOURCE_DIR)
  FetchContent_Declare(cursiv SOURCE_DIR ${CURSIV_SOURCE_DIR})
else()
  FetchContent_Declare(cursiv
      GIT_REPOSITORY https://github.com/ydaveluy/cursiv.git
      GIT_TAG        main)
endif()
FetchContent_MakeAvailable(cursiv)
```

Configure with `-DCURSIV_SOURCE_DIR=/path/to/cursiv` to use a local checkout;
scaffolded projects accept the same option.

## The targets you link

| Target | Provides |
|---|---|
| `cursiv::core` | parsing, syntax trees, linking and validation |
| `cursiv::lsp` | the language server and its providers |
| `cursiv::cli` | `run_cli`, for a [command-line tool](cli.md) |
| `cursiv::mcp` | the MCP server and its default tools |
| `cursiv::test` | the [test helpers](test.md) |
| `cursiv::bench` | the benchmark driver |
| `<ID>Generated` | the generated module of language `<ID>` |

## `cursiv_generate_language`

Generates the parser from your grammar header and defines the library
`<ID>Generated`, plus `<ID>_generate`, which writes the sources without
compiling them.

```cmake
cursiv_generate_language(ID         mylang
    GRAMMAR      mylang/core/MyLangGrammar.hpp
    NAMESPACE    mylang::MyLang
    EXTENSIONS   .mylang
    OUT          mylang/generated)
```

| Argument | Required | Meaning |
|---|---|---|
| `ID` | yes | language id and target prefix |
| `GRAMMAR` | yes | the header, spelled as an include path |
| `NAMESPACE` | yes | the grammar type, qualified: `ns::Type` |
| `OUT` | yes | absolute output directory, or a path under the caller's build tree |
| `EXTENSIONS` | no | file extensions associated with the language |
| `FILE_NAMES` | no | exact file names it claims, for a language like `Makefile` |
| `BASE` | no | id of a language declared earlier in CMake; adds its generated library and include roots as dependencies |
| `ENGINE` | no | `LL` or `LR`; defaults to `CURSIV_DEFAULT_ENGINE`, which is `LR`, the recommended engine |
| `INCLUDE_DIRS` | no | extra include roots; relative paths start at the calling source directory |
| `GENERATOR_ARGS` | no | additional arguments passed to the generator |

`NAMESPACE ns::Type` generates into `ns::generated::Type`, under
`<OUT>/Type/`, so languages with different type names can share `OUT`. A
relative `OUT` goes under `generated-src/` in the build tree: `OUT
mylang/generated` in `src/CMakeLists.txt` writes to
`build/src/generated-src/mylang/generated/`. The calling source directory is
also an include root.

`BASE` only adds build dependencies; declare grammar inheritance in C++ with
`cursiv::extends<BaseGrammar>`.

## `cursiv_package_language`

```cmake
cursiv_package_language(mylang)
```

In a threaded WebAssembly build, copies the `<ID>_lsp` module, `wasi-run.mjs`
and license notices into `<project>/vscode/server/` after each build. It does
nothing in native builds, or without that target or the project's `vscode/`
directory.

## `cursiv_add_benchmark`

```cmake
cursiv_add_benchmark(mylang SOURCES mylang/bench/main.cpp)
```

Creates `mylang_bench`, linked to `mylangLsp` and `cursiv::bench`, and adds
it to the aggregate `bench` target. Declare `mylangLsp` first.

## Options

| Option | Default | Use it to… |
|---|---|---|
| `CURSIV_DEFAULT_ENGINE` | `LR` | Select `LL` or `LR` when a language does not specify `ENGINE`. |
| `CURSIV_MIMALLOC` | `ON` | Link the mimalloc allocator into every executable. |
| `CURSIV_SOURCE_DIR` | — | Select a local checkout in the FetchContent setup above. |
| `CURSIV_HOST_TOOLS` | — | When cross-compiling, run the parser generators from this existing native build instead of building them in `host-tools/` inside the build directory. |
| `CURSIV_WASI_THREADS` | `ON` with the WASI toolchain | Build the threaded WebAssembly target, which the packaged language server needs. Set it only with the WASI toolchain. |
| `CURSIV_WASI_MAX_MEMORY` | `2147483648` (2 GiB) with WASI threads | Set the shared-memory limit in bytes. |
| `CURSIV_WASI_STACK` | `1048576` (1 MiB) with the WASI toolchain | Set the main thread's stack size in bytes. |
| `WASI_SDK` | Environment or conventional install location | Select the wasi-sdk directory; see [WebAssembly](../recipes/wasm.md). |

Sanitizer flags set for the whole build turn mimalloc off. If only one target
uses a sanitizer, configure with `-DCURSIV_MIMALLOC=OFF`, or set the
`CURSIV_OWN_ALLOCATOR` property of that target to `ON`.
