# CMake API

The [scaffolder](../learn/scaffold.md) creates a configured language project.
Use this reference when adding Cursiv to an existing project or changing its
build settings.

## Consuming cursiv

Add Cursiv with CMake FetchContent, then declare your language target.

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

In this configuration, `-DCURSIV_SOURCE_DIR=/path/to/cursiv` selects a local
checkout instead of fetching the tag. The generated project uses the same
convention.

## The targets you link

| Target | Provides |
|---|---|
| `cursiv::core` | the parse, the tree, references, validation |
| `cursiv::lsp` | the language server and its providers |
| `cursiv::cli` | `run_cli` |
| `cursiv::mcp` | the MCP server and its default tools |
| `cursiv::test` | the harness and the expectations |
| `cursiv::bench` | the language benchmark driver |
| `<ID>Generated` | one language's generated module |

## `cursiv_generate_language`

Runs the selected parser generator against your grammar header and defines a
library containing the generated code.

```cmake
cursiv_generate_language(ID         mylang
    GRAMMAR      mylang/core/MyLangGrammar.hpp
    NAMESPACE    mylang::MyLang
    EXTENSIONS   .mylang
    OUT          mylang/generated)
```

| Argument | Required | Meaning |
|---|---|---|
| `ID` | yes | language id and target prefix; defines `<ID>Generated` |
| `GRAMMAR` | yes | the header, spelled as an include path |
| `NAMESPACE` | yes | the grammar type, qualified: `ns::Type` |
| `OUT` | yes | absolute output directory, or a path under the caller's build tree |
| `EXTENSIONS` | no | served extensions, stored in `languageMetaData.fileExtensions` |
| `FILE_NAMES` | no | exact file names it claims, for a language like `Makefile` |
| `BASE` | no | id of a language declared earlier in CMake; adds its generated library and include roots as dependencies |
| `ENGINE` | no | `LL` or `LR`, the engine this language is generated with |
| `INCLUDE_DIRS` | no | extra include roots; relative paths start at the calling source directory |
| `GENERATOR_ARGS` | no | additional arguments passed to the selected generator |

A language that names no `ENGINE` follows `CURSIV_DEFAULT_ENGINE`, which is
`LR`.

`NAMESPACE ns::Type` generates into `ns::generated::Type`, with files under
`<OUT>/Type/`. Two languages with different type names can share `OUT`.
`BASE` supplies build dependencies; grammar inheritance itself is declared in
C++ with `cursiv::extends<BaseGrammar>`.

The calling source directory remains an include root with either form of
`OUT`. Relative output paths are placed under `generated-src/` in the build
tree: for example, `OUT mylang/generated` in `src/CMakeLists.txt` writes to
`build/src/generated-src/mylang/generated/`. When linking into shared libraries,
set `CMAKE_POSITION_INDEPENDENT_CODE`
if you need to override the default chosen by `BUILD_SHARED_LIBS`.

Defines two targets: the library `<ID>Generated`, and `<ID>_generate`, which
writes the sources without compiling them.

## `cursiv_package_language`

```cmake
cursiv_package_language(mylang)
```

For a threaded WebAssembly cross build, adds a post-build step to `<ID>_lsp`
that copies its module, `wasi-run.mjs` and license notices into
`<project>/vscode/server/`. The notices cover Cursiv, lsp-framework and mimalloc.
It requires that target and the project's `vscode/` directory. Native builds
are unaffected.

## `cursiv_add_benchmark`

```cmake
cursiv_add_benchmark(mylang SOURCES mylang/bench/main.cpp)
```

Creates `mylang_bench` and adds it to the aggregate `bench` target. Declare
`mylangLsp` first: the benchmark links that language-server library and
`cursiv::bench` so it can measure parsing, workspace and editor services.

## Options

| Option | Default | Use it to… |
|---|---|---|
| `CURSIV_DEFAULT_ENGINE` | `LR` | Select `LL` or `LR` when a language does not specify `ENGINE`. |
| `CURSIV_MIMALLOC` | `ON` | Enable the mimalloc allocator; use `OFF` to disable it. |
| `CURSIV_SOURCE_DIR` | — | Select a local checkout in the FetchContent setup above. |
| `CURSIV_WASI_THREADS` | `ON` with the WASI toolchain | Build the threaded WASI target required by the language server. Use it only with the WASI toolchain. |
| `CURSIV_WASI_MAX_MEMORY` | `2147483648` (2 GiB) with WASI threads | Set the shared-memory addressing limit in bytes; memory grows on demand. Existing caches keep their configured value. |
| `WASI_SDK` | Environment or conventional install location | Select the wasi-sdk directory; see [WebAssembly](../recipes/wasm.md). |

Global sanitizer flags disable mimalloc automatically. For sanitizers enabled
per target, configure with `-DCURSIV_MIMALLOC=OFF`, or exclude only the
instrumented target:

```cmake
set_property(TARGET my_app PROPERTY CURSIV_OWN_ALLOCATOR ON)
```

For example, configure a language project to use LL:

```bash
cmake -B build -DCURSIV_DEFAULT_ENGINE=LL
```

## Advanced: test grammars

### `cursiv_test_language`

Generates a language for tests. It accepts the same arguments, but defaults
`OUT` to `generated/<ID>/generated` and uses lower optimization for generated
parser code in Release builds. Relative output follows the same
`<current-build-directory>/generated-src/` layout as above.

Test languages compile their generated sources in two translation units
to avoid parsing the same tables repeatedly. Pass `SEPARATE_UNITS` when a test
specifically needs the production layout across separate translation units.
Generated test sources also use `-O1` in WASI Debug builds to fit WebAssembly
function limits; test code and framework libraries retain their Debug settings.

```cmake
cursiv_test_language(ID        toy
    GRAMMAR   fixtures/ToyGrammar.hpp
    NAMESPACE toy::Toy)
```

Use this helper when a test needs its own grammar. For testing a language's
parsing and validation, start with [Test your language](../learn/testing.md).

## Advanced: cross-compilation tools

### `cursiv_host_tools` target

Cross builds automatically provide this target for native parser generators.
Use the normal configure and build commands; a native build is not required
first. Host generators use Release mode and the language project's C++ standard.

## Contributing to Cursiv

These options control builds of the framework repository:

| Option | Default | Effect |
|---|---|---|
| `CURSIV_BUILD_TESTS` | `ON` when Cursiv is the top-level project | Build the framework's tests and their example-language dependencies. |
| `CURSIV_WARNINGS_AS_ERRORS` | `ON` for the top-level project, `OFF` as a dependency | Treat compiler warnings in Cursiv as errors. |
