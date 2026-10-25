# Create a project

Use CMake 3.30 or newer, Git and a C++20 compiler with `std::format` and
`musttail` support. CI tests GCC 16 and Clang 22. The first build downloads
Cursiv and its dependencies.

## Create, build and run

Download the scaffolder and create a language named `MyLang`:

```bash
curl -fsSLO https://raw.githubusercontent.com/ydaveluy/cursiv/main/docs/cursiv-new.cmake
cmake -DNAME=MyLang -P cursiv-new.cmake
cd mylang
cmake -B build
cmake --build build --parallel
./build/src/mylang_cli example/hello.mylang
```

The CLI reports `example/hello.mylang: ok`. The sample declares two people
and greets each by name; its references resolve without custom services.

Run the generated tests and inspect the syntax tree:

```bash
ctest --test-dir build --output-on-failure
./build/src/mylang_cli --dump example/hello.mylang
```

Build again with `cmake --build build --parallel` after editing the grammar.
CMake handles parser generation automatically.

## What you got

```
mylang/
├── CMakeLists.txt          project and dependency configuration
├── CMakePresets.json       GCC, Clang and WASI in Debug/Release
├── example/hello.mylang    a document that parses
├── src/
│   ├── CMakeLists.txt      language and executable targets
│   └── mylang/
│       ├── core/
│       │   ├── ast.hpp                 your structs
│       │   ├── MyLangGrammar.hpp       your grammar
│       │   ├── CoreServices.hpp        your services
│       │   ├── CoreModule.cpp          service configuration
│       │   └── validation/             your checks
│       ├── cli/main.cpp
│       ├── lsp/{LspModule,main}.cpp
│       └── bench/main.cpp
├── tests/grammar_test.cpp
└── vscode/                 the editor client
```

Start with `core/ast.hpp` for your data types and `MyLangGrammar.hpp` for
syntax. The validation directory contains the sample's naming and uniqueness
checks. The tutorial replaces all three together.

The generated sources appear under the build directory on the first build
(`build/src/generated-src/mylang/generated/` with the default layout). They
are rewritten whenever the grammar changes. Never edit them.

The `cli/`, `lsp/`, `bench/` and `vscode/` directories are included by default.
Delete the ones you do not want. CMake omits the matching targets and tests
when `cli/`, `lsp/` or `bench/` is absent. The benchmark requires LSP services,
so removing `lsp/` also disables the benchmark.

## Open it in VS Code

Follow [Ship an editor extension](editor-integration.md) to install the editor
prerequisites and launch with **F5**. A second window opens on `example/` with
the extension loaded.

## Scaffold options

| Option | Default | Meaning |
|---|---|---|
| `-DNAME=` | *required* | Grammar type; it and its lowercase namespace must be non-reserved C++ identifiers. |
| `-DEXT=` | `.<id>` | Language file extension, starting with a dot. |
| `-DDIR=` | `<id>` | Directory to create; it must be empty. |
| `-DCURSIV_TAG=` | `main` | Cursiv revision to fetch. |

The lowercase name becomes the language id. For example, this creates a
`recipe` project whose files use `.rcp`:

```bash
cmake -DNAME=Recipe -DEXT=.rcp -DDIR=recipe-lang -P cursiv-new.cmake
```

## Next

The [Recipe tutorial](walkthrough.md) starts a separate project with its own
sample and AST.
