# Cursiv for Agents

This is a C++20 language-engineering framework with LL and LR parser backends.

For project documentation, read:

- `README.md`: Build, tests, benchmarks, and WebAssembly.
- `docs/reference/grammar.md`: Grammar DSL and parser contracts.
- `docs/reference/ast.md`: AST types and construction.
- `docs/reference/services.md`: Services, linking, workspaces, and LSP.
- `docs/reference/test.md`: Language testing helpers.

Source directories:

- `src/core/cursiv/`: Grammar model, syntax trees, linking, and workspaces.
- `src/engines/cursiv/`: LL/LR generators, runtimes, and shared code.
- `src/cli/cursiv/`: Command-line support for language tools.
- `src/lsp/cursiv/`: Language-server services.
- `src/mcp/cursiv/`: Model Context Protocol server and language tools.
- `src/test/cursiv/`: Parse, validation, and editor test helpers.
- `src/bench/cursiv/`: Benchmark harness and performance counters.
- `src/fuzz/cursiv/`: Grammar-based input generation and tree checks.
- `examples/`: Complete example languages.
- `docs/demo/`: The "Try it online" page of the documentation.
- `tests/`: Framework tests.
- `bench/`: Benchmark executables and corpora.
- `bench/recovery_metric/`: Error-recovery quality reports and internal support.
- `templates/`: Scaffolding templates for new language projects.
- `cmake/`: Build integration and language generation.
- `tools/`: Build, packaging, and development scripts.

## Changing code

- Fix the existing code and fit into it. Do not add a helper, member, flag,
  code path, check, wrapper, friend or thread-local next to one that already
  does nearly the same: change that one, and delete what becomes unused.
- Fix the cause with one general rule, not a special case per input.
- Keep comments short: one to three lines saying what the code does, or the
  one rule a caller must know. No reasoning, history, proofs, measurements or
  lists of edge cases.
- A fix never adds a sentence to an existing comment or doc section: rewrite
  it so it reads as written once.

## Naming

The public surface follows one rule; `detail` and the engines' internals are
not held to it.

- Types are `PascalCase`. Member functions and fields are `camelCase`, private
  fields `_name`. Free functions and grammar DSL names are `snake_case`.
- Constants are `kName`. Enumerators are `PascalCase`.
- A named slot of an AST node is a `property`.

## VS Code extensions

`examples/*/vscode/` contains TypeScript extension projects, built with npm.
See each extension's `package.json` and `docs/learn/editor-integration.md`.
