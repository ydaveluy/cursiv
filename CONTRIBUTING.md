# Contributing to Cursiv

Contributions can improve the parser engines, language services, examples or
documentation. To build your own language with the framework, start with the
[user guide](docs/learn/scaffold.md).

## Report a problem

For security vulnerabilities, follow the [security policy](SECURITY.md) to
report privately.

Open an [issue](https://github.com/ydaveluy/cursiv/issues) with the expected
behavior, actual behavior and steps to reproduce it. For a parsing problem,
include the smallest grammar and input that demonstrate it, along with your
compiler version and build preset.

## Build and test

You need Git, CMake 3.30 or later, Ninja and a C++20 compiler with `std::format`
and `musttail` support. CI tests GCC 16 and Clang 22.

Run from the repository root:

```sh
cmake --preset gcc-release
cmake --build --preset gcc-release --parallel 4
ctest --preset gcc-release --output-on-failure --parallel 4
```

The build fetches its dependencies. Use `gcc-debug`, `clang-release` or
`clang-debug` for another configuration. Clang presets use `clang++`; pass
`-DCMAKE_CXX_COMPILER=clang++-22` when your installation uses that name.

These options control builds of the framework repository:

| Option | Default | Effect |
|---|---|---|
| `CURSIV_BUILD_TESTS` | `ON` when Cursiv is the top-level project | Build the framework's tests and their example-language dependencies. |
| `CURSIV_WARNINGS_AS_ERRORS` | `ON` for the top-level project, `OFF` as a dependency | Treat compiler warnings in Cursiv as errors. |

Cross builds provide the `cursiv_host_tools` target for native parser
generators. These use Release mode and the language project's C++ standard;
the regular build invokes them automatically.

The test suite includes both LL and LR grammars. Add a regression test for a
bug fix; shared parser contracts belong in `tests/engines/shared/`.

Framework fixtures use `cursiv_test_language` internally. It accepts the
arguments of `cursiv_generate_language`, defaults `OUT` to
`generated/<ID>/generated`, and compiles the generated parser, metadata and
reflection together. Pass `SEPARATE_UNITS` when a fixture needs the production
layout across separate translation units. Native generated test code uses
`-O0` in Release; WASI uses `-O1`, including in Debug, to fit WebAssembly
function limits.

Reuse an existing test grammar when adding another input or AST scenario.
Compatible variants can share entry rules; keep separate grammars when the
global word boundary, indentation policy or an isolated lexer table is the
contract under test. Avoid generating another backend solely as an oracle when
the expected tree, diagnostics and source spans can be stated directly.
Related cases share a test executable and parser instances. Keep a prefixed
input string alive for as long as its parse result or CST is inspected.

Keep stress fixtures just beyond the boundary they exercise. Large runtime
tables can be built as test data: the wide-position completion test still
crosses position 16384 without compiling thousands of repeated parser functions.
The bounded-repeat cases cover interval boundaries and directive shapes rather
than a Cartesian product of equivalent intervals.

The framework's test build shares generated parser and generator compilation
across groups of four languages. Fixtures checking separate translation units,
compiler limits or namespace/include isolation retain independent compilation.
On Linux and WASI, thin archives reference each shared test object once, and
unused modules are removed when linking a test executable.
Each CI configuration runs its complete test suite. The example languages
retain their production compilation and optimisation settings.

In VS Code, install the recommended CMake Tools and clangd extensions, select a
configure preset, and configure the project. CMake Tools copies the active
build's `compile_commands.json` to the source root, where clangd reads its
compiler flags and generated include paths. Clangd also formats the sources
using `.clang-format`. Microsoft C/C++ language services are disabled for this
workspace to avoid duplicate completion and diagnostics.

For WASI Release builds, install Binaryen 120 or later and put `wasm-opt` on
`PATH`; CI uses the Ubuntu 26.04 package. SDK Clang runs it automatically after
linking at the selected optimization level (`-O3` for Release), including for
benchmarks and CLI tools. Unit test executables pass `--no-wasm-opt` to skip it.
WASI Debug builds do not run it with their default flags. Native builds and
native parser generators are unaffected.

To package the example extensions, build the `wasi-release` preset and run
`tools/package-vsix.sh build-wasi dist-vsix`. Packaging also requires Node.js
with npm; it copies the built modules and notices without optimizing again.

Editor packaging keeps scoped npm overrides for `@azure/identity` 4.13.0
and `uuid` ^11.1.1 to avoid
[the MSAL advisory](https://github.com/advisories/GHSA-xcx6-jx8p-x3h5) and
[the UUID advisory](https://github.com/advisories/GHSA-w5hq-g745-h8pq).
Remove these overrides when the upstream dependency tree passes `npm audit`
and VSIX packaging without them.

## Find the relevant code

| Area | Implementation | Tests |
|---|---|---|
| Grammar, AST and workspaces | `src/cursiv/core/` | `tests/core/` |
| Parser engines | `src/cursiv/engines/` | `tests/engines/` |
| Editor services | `src/cursiv/lsp/` | `tests/lsp/` |
| Build integration | `cmake/` | `tests/integration/` |
| Example languages | `examples/` | Each example's `tests/` directory |

Follow the surrounding code style and the repository's `.clang-format`.
Specialized contributor notes cover [LSP services](src/cursiv/lsp/README.md)
and [allocator integration](cmake/patches/README.md).

## Update documentation

Edit the user guides in `docs/` and the relevant README files. Describe the
current behavior, with a small example when it helps. Keep implementation
details in clearly marked contributor sections.

In a Python virtual environment, install the documentation tools and check the
site:

```sh
python -m pip install -r requirements-docs.txt
mkdocs build --strict
```

Run `mkdocs serve` to preview the site locally.

## Update the scaffolder

After editing `templates/project/`, `.clang-format`, `CMakePresets.json`,
`cmake/wasi.cmake` or `cmake/cursiv-new.cmake.in`, regenerate the distributed
script:

```sh
cmake -DCURSIV_TAG=main -P cmake/GenerateScaffold.cmake
```

Include the regenerated `docs/cursiv-new.cmake` in the change; do not edit it
directly.

## Propose a change

Keep each pull request focused on one problem. Explain the resulting behavior
and list the checks you ran. Update the user documentation when changing a
public API or language feature.

For dependency pull requests created by GitHub Actions, select **Approve
workflows to run** on the pull request when GitHub requests it. This lets the
usual checks run before merging.
