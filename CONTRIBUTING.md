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

`CURSIV_BUILD_TESTS` builds the framework tests; `CURSIV_WARNINGS_AS_ERRORS`
treats warnings as errors. Both default to `ON` for repository builds and
`OFF` when Cursiv is a dependency.

Add a regression test for each bug fix. Shared LL/LR contracts belong in
`tests/engines/shared/`. Reuse grammars and executables for related cases;
keep separate fixtures for distinct global policies or compiler contracts.
Generate only the rules a variant tests, and keep stress cases just beyond
the boundary they exercise. Keep input text alive while inspecting its AST/CST.

Internal fixtures use `cursiv_test_language`; `SEPARATE_UNITS` preserves
independent generated translation units when that is the contract under test.

In VS Code, use the recommended CMake Tools and clangd extensions. Select a
preset and configure; CMake Tools supplies `compile_commands.json` for clangd.

For WebAssembly prerequisites, see [WebAssembly](docs/recipes/wasm.md).

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

After editing `templates/project/`, `CMakePresets.json`,
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
