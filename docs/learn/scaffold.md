# Create a project

Install the [required tools](index.md#before-you-start) first.

## Create, build and run

Download the scaffolder and create a language named `MyLang`. The first build
also downloads Cursiv and its dependencies.

```bash
curl -fsSLO https://raw.githubusercontent.com/ydaveluy/cursiv/main/docs/cursiv-new.cmake
cmake -DNAME=MyLang -P cursiv-new.cmake
cd mylang
cmake -B build
cmake --build build --parallel
./build/src/mylang_cli example/hello.mylang
```

The CLI reports `example/hello.mylang: ok`. The sample declares two people
and greets each by name.

Run the tests and print the syntax tree:

```bash
ctest --test-dir build --output-on-failure
./build/src/mylang_cli --dump example/hello.mylang
```

After a grammar change, build again with `cmake --build build --parallel`:
the build regenerates the parser.

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

You mostly edit three places: `core/ast.hpp` for the data types,
`MyLangGrammar.hpp` for the syntax and `validation/` for the checks. The
tutorial replaces all three. Generated sources go under `build/`; never edit
them.

Delete `cli/`, `lsp/`, `bench/` or `vscode/` if you do not need them. CMake
then skips the matching targets and tests. The benchmark needs `lsp/`.

## Open it in VS Code

Follow [Ship an editor extension](editor-integration.md) to install the
editor tools, then press **F5**. A second window opens on `example/` with the
extension loaded.

## Scaffold options

| Option | Default | Meaning |
|---|---|---|
| `-DNAME=` | *required* | Grammar type. It and its lowercase form must be valid, non-reserved C++ identifiers. |
| `-DEXT=` | `.<id>` | File extension, starting with a dot. |
| `-DDIR=` | `<id>` | Directory to create; it must be empty. |
| `-DCURSIV_TAG=` | `main` | Cursiv revision to fetch. |

The lowercase name is the language id. For example, this creates a `recipe`
language whose files end in `.rcp`:

```bash
cmake -DNAME=Recipe -DEXT=.rcp -DDIR=recipe-lang -P cursiv-new.cmake
```

## Next

The [tutorial](walkthrough.md) creates a new project and builds a recipe
language step by step.
