# @CURSIV_NEW_CLASS@

A language project created with [Cursiv](https://ydaveluy.github.io/cursiv/).
Start with the sample, then edit the grammar and AST to define your language.

## Build and run

You need CMake 3.30 or later, Git and a C++20 compiler with `std::format` and
`musttail` support. Cursiv's CI uses GCC 16 and Clang 22.

```bash
cmake -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/src/@CURSIV_NEW_ID@_cli example/hello@CURSIV_NEW_EXT@
```

CMake generates the parser automatically, including when the grammar changes.
Generated files are under `build/src/generated-src/`; do not edit them.

## The sample language

```text
person Alice
person Bob

Hello Alice!
Hello Bob!
```

`person` declares a name and `Hello` refers to it. An unknown name produces a
diagnostic. The same references support go-to-definition and rename in the
editor.

Use `--dump` to inspect the concrete syntax tree (CST):

```bash
./build/src/@CURSIV_NEW_ID@_cli --dump example/hello@CURSIV_NEW_EXT@
```

The CLI returns `0` for no errors, `1` for document errors, `2` for invalid
arguments or paths, and `3` for an internal or output error.

## Customize the language

| To change… | Edit |
|---|---|
| Syntax | `src/@CURSIV_NEW_ID@/core/@CURSIV_NEW_CLASS@Grammar.hpp` |
| AST types | `src/@CURSIV_NEW_ID@/core/ast.hpp` |
| Validation rules | `src/@CURSIV_NEW_ID@/core/validation/` |
| Language services | `src/@CURSIV_NEW_ID@/core/CoreServices.hpp` and `CoreModule.cpp` |
| CLI behavior | `src/@CURSIV_NEW_ID@/cli/main.cpp` |
| Editor features | `src/@CURSIV_NEW_ID@/lsp/LspModule.cpp` |
| Tests | `tests/grammar_test.cpp` |

Follow the [language tutorial](https://ydaveluy.github.io/cursiv/learn/walkthrough/)
to replace the sample with a complete language.

## Open the editor

Install Node.js, npm and the recommended VS Code extensions, including CMake
Tools and clangd. Native servers support VS Code 1.86 or later; WASI servers
require 1.101 or later. Open this project folder in VS Code, configure the
CMake build and press **F5**. The launch task builds the server and client,
then opens `example/` in a development window.

Native GCC/Clang presets run the native server. WASI presets run a WebAssembly
server through Node. See
[Editor integration](https://ydaveluy.github.io/cursiv/learn/editor-integration/)
for customization and packaging.

## Measure performance

```bash
cmake --build build --target @CURSIV_NEW_ID@_bench
./build/src/@CURSIV_NEW_ID@_bench example/hello@CURSIV_NEW_EXT@ --iterations 5
```

The benchmark measures parsing, document builds and installed editor providers.
Use `--json bench.json` or `--csv bench.csv` to export results, and `--help` for
all options. Its entry point is `src/@CURSIV_NEW_ID@/bench/main.cpp`.

## Optional components

The CLI, server, benchmark and VS Code extension are included by default.
Remove the component folders you do not need; CMake omits their targets and
tests. Removing `lsp/` also disables the benchmark, which uses LSP services.
